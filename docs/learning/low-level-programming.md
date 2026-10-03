# Low-level programming

Talking to hardware, controlling memory layout, and caring about bytes. Core keeps the C systems-programmer toolbox: volatile accesses, packed structs, `sizeof`/`alignof`, and honest endianness.

## Memory-mapped IO (MMIO)

Hardware registers live at fixed physical addresses. You read and write them through pointers — with **volatile** semantics, because the device changes the memory behind the compiler's back:

```core
@packed struct UartRegs {
    data: u32       // offset 0
    status: u32     // offset 4
    baud: u32       // offset 8
}

func tx_ready(regs: ptr<UartRegs>) -> bool {
    return unsafe { volatile_load(&regs.status) & 1 } != 0
}

func putchar(regs: ptr<UartRegs>, c: char) {
    while !tx_ready(regs) { }
    unsafe {
        volatile_store(&regs.data, c as u32)
    }
}
```

Key points:

- `volatile_load`/`volatile_store` (in `unsafe`) forbid the optimizer from caching or reordering accesses — required for device memory, wrong for ordinary variables.
- A busy-wait polling loop (`while !tx_ready(...) { }`) is the classic driver pattern; volatile makes it actually re-read.
- Where does the pointer come from? Bare metal: cast the physical address (`unsafe { 0x10000000 as ptr<UartRegs> }`). Hosted testing: allocate (the block above uses `core_rt_alloc_aligned`) or `mmap` via FFI (see [ffi.md](ffi.md)).

## @packed structs: exact layout

```core
@packed struct FrameHeader {
    magic: u16      // offset 0
    version: u8     // offset 2
    flags: u8       // offset 3
}
// sizeof == 4, alignof == 1 — exactly as written, no padding
```

- Without `@packed`, Core uses natural C alignment (a `u16` after a `u8` gets padded). Fine for internal data; **wrong** for wire formats and device layouts.
- With `@packed`, fields sit at their declared offsets — `sizeof` equals the sum of field sizes, `alignof` is 1.
- Packed structs in arrays have unaligned elements: correct but potentially slower (or faulting on strict-alignment targets — know your platform).

## sizeof and alignof

Compile-time builtins, target-true:

```core
say sizeof(u32)          // 4
say sizeof(FrameHeader)  // 4
say sizeof([UartRegs; 4])// 48 — packed elements, no padding
say alignof(u64)         // 8
```

Use them instead of hardcoded offsets: `sizeof(FrameHeader)` in a parse loop survives field additions.

## Endianness

The language is **endianness-agnostic**; the *target* isn't. Detect it:

```core
func is_little_endian() -> bool {
    x: u32 = 1
    p: ptr<u8> = unsafe { (&x) as ptr<u8> }
    return *p == 1    // little-endian stores the low byte first
}
```

Rules of the road:

- **In-memory data** (structs you load/store on the same machine) needs no conversion.
- **Wire/file formats** specify byte order — serialize field by field with shifts, not raw casts:

```core
func write_u32_be(dst: ptr<u8>, v: u32) {
    dst[0] = (v >> 24) as u8
    dst[1] = (v >> 16) as u8
    dst[2] = (v >> 8) as u8
    dst[3] = v as u8
}
```

- `bswapq` via inline asm (see [inline-assembly.md](inline-assembly.md)) converts 64-bit words wholesale.

## Driver pattern: the shape of hardware code

```core
// 1. The register block as a packed struct (offsets per the datasheet)
// 2. init: poke configuration registers
// 3. send/recv: poll status, touch data, in volatile
// 4. a thin safe wrapper so the rest of the program never sees unsafe
```

Simulating the block on the host (heap instead of device memory) lets you unit-test the logic — the `examples/driver` example does exactly this:

```core
// The declarations above plus this main form the complete driver simulation.
import memory

extern func core_rt_alloc_aligned(size: usize, align: usize) -> ptr<u8>
extern func core_rt_free(p: ptr<u8>) -> void

func main() {
    regs: ptr<UartRegs> = unsafe {
        core_rt_alloc_aligned(sizeof(UartRegs), 4) as ptr<UartRegs>
    }
    unsafe {
        volatile_store(&regs.baud, 115200)
        volatile_store(&regs.status, 1)   // simulate: transmitter ready
    }
    puts_str(regs, "driver online")
    say regs.baud                          // 115200
    free(regs)                             // free<T> takes ptr<UartRegs> directly
}
```

## Bit fields in control registers

Reading datasheets into shifts/masks (note the explicit `as u32` — shift results are `i32`):

```core
mut ctrl: u32 = 0
ctrl |= 1 as u32 << 3        // set bit 3 (enable)
ctrl |= 2 as u32 << 8        // field at bits 8-15 (divisor low)
ctrl &= ~(1 as u32 << 3)     // clear bit 3
say ctrl == 512              // true
```

## Complete working example

```core
// lowlevel.cr - MMIO-style UART, layout introspection, endianness
import memory

@packed struct UartRegs {
    data: u32
    status: u32
    baud: u32
}

struct FrameHeader {
    magic: u16
    version: u8
    flags: u8
}

extern func core_rt_alloc_aligned(size: usize, align: usize) -> ptr<u8>

func tx_ready(regs: ptr<UartRegs>) -> bool {
    return unsafe { volatile_load(&regs.status) & 1 } != 0
}

func putchar(regs: ptr<UartRegs>, c: char) {
    while !tx_ready(regs) { }
    unsafe {
        volatile_store(&regs.data, c as u32)
    }
}

func puts_str(regs: ptr<UartRegs>, s: string) {
    for i in 0..len(s) {
        putchar(regs, s[i])
    }
}

func is_little_endian() -> bool {
    x: u32 = 1
    p: ptr<u8> = unsafe { (&x) as ptr<u8> }
    return *p == 1
}

func main() {
    say sizeof(FrameHeader)    // 4
    say alignof(FrameHeader)   // 2
    say sizeof(UartRegs)       // 12
    say alignof(UartRegs)      // 1
    say is_little_endian()     // true (x86-64)

    regs: ptr<UartRegs> = unsafe {
        core_rt_alloc_aligned(sizeof(UartRegs), 4) as ptr<UartRegs>
    }
    unsafe {
        volatile_store(&regs.baud, 115200)
        volatile_store(&regs.status, 1)
    }
    puts_str(regs, "driver online")
    say regs.baud              // 115200
    free(regs as ptr<UartRegs>)

    mut ctrl: u32 = 0
    ctrl |= 1 as u32 << 3
    ctrl |= 2 as u32 << 8
    ctrl &= ~(1 as u32 << 3)
    say ctrl == 512            // true
}
```

## Common mistakes

- **Forgetting volatile on device accesses.** The compiler will happily cache the status register in a register and loop forever.
- **Volatile on ordinary variables for "thread safety".** It doesn't synchronize — use atomics ([concurrency.md](concurrency.md)).
- **Non-packed structs for hardware layouts.** Silent padding corrupts the offsets — `@packed` + `sizeof` checks.
- **Assuming endianness.** `is_little_endian()` or byte-wise serialization; never cast a `ptr<u32>` over wire bytes.
- **Shift literals in u32 context.** `ctrl |= 1 << 3` fails (`i32` shift); write `1 as u32 << 3`.
- **Unaligned casts.** `p as ptr<u64>` on a misaligned `p` faults on some targets; use `alloc_aligned` and check `alignof`.

## Performance notes

- Volatile accesses are never cached or merged — that's the point, and the cost. Keep them out of memcpy-class bulk paths.
- Packed struct access can cost extra loads/stores (misalignment); copy into a natural struct for heavy processing.
- `sizeof`/`alignof` fold to constants; zero runtime cost.
- Polling loops burn a core — fine for embedded; on hosted OSes prefer blocking FFI calls or threads ([concurrency.md](concurrency.md)).

## When to use / not use

- **Use** these tools when a *datasheet* is your API: UARTs, SPI/I²C devices, PCI config spaces, framebuffers.
- **Don't** reinvent memory allocation with raw byte casts — the `memory` module's typed allocators are safer (see [memory-management.md](memory-management.md)).
- **Don't** ship hosted programs full of MMIO pointers; behind an OS, that's [os-development.md](os-development.md) territory in reverse — ask the kernel instead.
- Going further down: [freestanding-development.md](freestanding-development.md) removes the OS entirely.

## Exercises

1. Write `write_u32_be` / `read_u32_be` over a `[u8; 4]` and round-trip five values including 0 and 0xFFFFFFFF.
2. Print every byte of a `u64` through a `ptr<u8>` cast; confirm `is_little_endian` matches the order you see.
3. Define a packed and an unpacked version of a 3-field struct; print both `sizeof`s and explain each padding decision.
4. Simulate a status register: volatile-store 0 into a variable, poll with a bounded loop (max 100 tries) until a second "interrupt" store sets the ready bit.
5. Write `set_bit`, `clear_bit`, `toggle_bit`, `test_bit` helpers over `u32` (remember `as u32` on shift literals) and test each.

Next: [Freestanding development](freestanding-development.md).
