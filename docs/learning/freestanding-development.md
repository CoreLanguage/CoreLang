# Freestanding development

Freestanding Core = **no OS runtime, no libc, your entry point**. The compiler emits a plain object file you link with `ld` and a linker script — this is how you write kernels, bootloaders, and embedded firmware in Core.

## What changes

| Hosted (normal) | Freestanding |
|---|---|
| `main` called by the C runtime | your `_start`-style entry runs first |
| `corert.o` linked in (printing, malloc, pthreads) | **nothing** — no runtime |
| syscalls via libc | you talk to hardware or make your own syscalls |
| `std/` modules using runtime (`memory`, `thread`, `time`) | unavailable — only pure-computation code |

The `prelude` still works for anything that doesn't hit the runtime... carefully: `say`/`panic` call runtime functions that don't exist in freestanding links. Compile freestanding code as **leaf logic** (algorithms, data structures — see [algorithms.md](algorithms.md)) plus your own hardware layer.

## Building

```bash
core compile kernel.elf main.cr --freestanding --emit-object
```

- `--freestanding` — no runtime, no libc, no hosted `main` handling
- `--emit-object` — stop at the object file (`kernel.elf.coreobj.o`); *you* link it
- add `--target=x86_64|aarch64|riscv64` for cross compilation (same object output)

## The entry point

Declare your entry with `@link_name("_start")`. A freestanding entry **must not return** — there's nothing to return to:

```core
@link_name("_start") func kmain() -> never {
    // ... your code runs first, CPU in whatever state the boot protocol left
    halt()
}

func halt() -> never {
    unsafe {
        asm("cli", "")      // disable interrupts (x86)
    }
    halt()                  // loop forever
}
```

`-> never` documents "does not return" and the compiler enforces reachability (see [types.md](types.md)).

## Linking with ld + a linker script

The object needs a link address and entry metadata — that's the linker script's job (`linker.ld`):

```ld
/* linker.ld - place the kernel at 1 MiB, standard for x86 boot protocols */
ENTRY(_start)
SECTIONS
{
    . = 1M;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
```

```bash
ld -T linker.ld -o kernel.elf kernel.elf.coreobj.o
```

Inspect the result:

```bash
nm kernel.elf | grep _start     # your entry symbol is there
objdump -d kernel.elf | head    # real machine code, cli included
```

Run it in an emulator under your chosen boot protocol (multiboot via GRUB, or a hand-rolled bootloader with `qemu-system-x86_64`).

## The complete kernel example

`examples/kernel/main.cr` — prints to the VGA text framebuffer, then halts:

```core
// Build with:
//   core compile kernel.elf main.cr --freestanding --target=x86_64 --emit-object
// then link:  ld -T linker.ld -o kernel.elf kernel.elf.coreobj.o
// and run in an emulator (qemu-system-x86_64 with a boot protocol of your choice).
@link_name("_start") func kmain() -> never {
    vram: ptr<u16> = unsafe { 0xB8000 as ptr<u16> } // VGA text buffer
    msg = "Hello from Core bare metal!"
    color: u16 = 0x0F00 // white on black
    for i in 0..26 {
        unsafe {
            volatile_store(vram + i, color + msg[i] as u16)
        }
    }
    halt()
}

func halt() -> never {
    unsafe {
        asm("cli", "")
    }
    halt()
}
```

See [os-development.md](os-development.md) for the VGA framebuffer format and what an OS needs beyond this first step.

## What you lose without the runtime — and replacements

| Lost | Replacement |
|---|---|
| `say`/`print` | write bytes to a port (UART) or the VGA framebuffer yourself |
| `memory.alloc` | your own allocator over a static `[u8; N]` bump pool (see the arena in [memory-management.md](memory-management.md)) |
| `thread`, `time` | nothing (bring your own scheduler/timer) |
| `panic` | a `never` function that prints via your output path and halts |
| libc (`memcpy`...) | the compiler emits calls only when your code declares them; write your own loops or asm |

Globals work (they're zero-initialized by the linker's `.bss`); string literals work (they're in `.rodata`).

## Cross compiling

```bash
core compile cross.bin main.cr --target=aarch64
# cross-compiling for aarch64: wrote object file cross.bin.coreobj.o
#   (link it with a aarch64 toolchain, or pass --link-arg=... for a cross linker)
```

Produces a genuine AArch64 relocatable object. Link it with a matching cross toolchain (`aarch64-linux-gnu-ld`):

```bash
core compile cross main.cr --target=aarch64 --link-arg=-m --link-arg=aarch64linux
```

## Complete working example

The kernel above builds end-to-end — verified:

```bash
core compile kernel.elf main.cr --freestanding --emit-object   # built: kernel.elf
nm kernel.elf | grep _start                                     # 0000000000401000 T _start
ld -T linker.ld -o kernel.elf kernel.elf.coreobj.o
```

## Common mistakes

- **Returning from `_start`.** The CPU jumps into the void — mark the entry `-> never` and end with a halt loop.
- **Using `say`/`alloc` freestanding.** Link errors (`undefined reference to core_rt_*`) — freestanding code must be self-sufficient.
- **Forgetting `--emit-object`.** Without it the compiler tries a hosted link; with `--freestanding` it should always pair with `--emit-object`.
- **Wrong link address.** The linker script must match the boot protocol's expectations (1 MiB for many x86 protocols), or the emulator triple-faults instantly.
- **Testing only in your head.** Use QEMU — it shows triple-faults and lets you log port I/O (`-d int -D log`).

## Performance notes

- Freestanding binaries are *just* your code: no runtime init, no libc startup, entry to first instruction is immediate.
- `-Os` matters at these sizes; a VGA hello is a few hundred bytes of code.
- Whole-program compilation is your friend: everything inlines across "modules" even freestanding.

## When to use / not use

- **Use freestanding** for kernels, bootloaders, embedded firmware, and understanding what "the runtime" actually does.
- **Don't** use it for hosted tools — the runtime and stdlib exist for a reason (see [projects.md](projects.md)).
- Once your freestanding project grows a console, allocator, and interrupt table, you're writing an OS — see [os-development.md](os-development.md).

## Exercises

1. Build the kernel example and confirm `_start` appears in `nm` output.
2. Write `linker.ld` yourself (entry at 1 MiB), link, and check the link address with `nm kernel.elf | head`.
3. Add a `write_string(msg: string, row: i32)` function to the kernel and call it for two rows — rebuild and confirm both calls are inlined at `-O2` (`objdump -d | grep -c call`).
4. Cross-compile the same source with `--target=aarch64` and verify the object with `file`.
5. Replace `cli`+loop with a `hlt`-based halt (`asm_volatile("hlt", "")`) — document why `hlt` alone isn't enough with interrupts enabled.

Next: [OS development](os-development.md).
