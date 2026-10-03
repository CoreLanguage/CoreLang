# OS development

This page extends [freestanding-development.md](freestanding-development.md): from "code that runs on bare metal" toward "code that runs *other* code". Core is a fine language for a hobby kernel — small, manual, LLVM-backed — and this tutorial is honest about what the *language* gives you (types, codegen) versus what an *OS* brings (stacks, interrupts, memory management — the parts you build).

## What an OS actually brings (and you must build)

Running on bare metal, the boot protocol hands you a CPU in a known state and *nothing else*:

1. **A stack.** The boot environment may not give you one — load a pointer into `rsp` first thing.
2. **Interrupt handling.** The IDT (x86) must be built and loaded before any hardware works; a fault without one triple-faults the machine.
3. **Memory management.** You find your own free RAM (multiboot info), then build allocator(s) — the arena from [memory-management.md](memory-management.md) is your first one.
4. **Output.** Nobody prints for you — VGA text or a serial port (see below).

## The VGA text framebuffer

The x86 text mode framebuffer is at `0xB8000`: 80×25 cells of 2 bytes each — character, then attribute (foreground | background in low/high nibble):

```
cell i at 0xB8000 + i*2:
  byte 0: ASCII code
  byte 1: attribute (high nibble: bg, low nibble: fg)
```

As a Core struct over raw memory:

```core
const VGA: u64 = 0xB8000
const COLS: i32 = 80
const ROWS: i32 = 25

func vga_cell(i: i32) -> ptr<u16> {
    return unsafe { (VGA + (i as u64) * 2) as ptr<u16> }
}

func put_cell(i: i32, c: char, fg: u8, bg: u8) {
    attr: u16 = ((bg as u16) << 12) + ((fg as u16) << 8)
    unsafe {
        volatile_store(vga_cell(i), attr + (c as u16))
    }
}

func clear(fg: u8, bg: u8) {
    for i in 0..(COLS * ROWS) {
        put_cell(i, ' ', fg, bg)
    }
}

func write(s: string, row: i32, fg: u8, bg: u8) {
    for i in 0..len(s) {
        put_cell(row * COLS + i, s[i], fg, bg)
    }
}
```

Every access is `volatile` — the framebuffer isn't ordinary RAM (reads have side effects on some hardware, and the compiler must not merge these stores).

## A minimal kernel skeleton

```core
// kernel.cr — build + link exactly as in freestanding-development.md
const VGA: u64 = 0xB8000
const COLS: i32 = 80

func vram() -> ptr<u16> {
    return unsafe { VGA as ptr<u16> }
}

@link_name("_start") func kmain() -> never {
    // 0. stack: with a multiboot bootloader a usable stack exists; from a
    //    hand-rolled boot sector, load your own rsp first (inline asm).
    // 1. paint the screen
    attr: u16 = 0x0F00          // white on black
    mut i: u64 = 0
    msg = "Core OS says hello"
    while i < len(msg) as u64 {
        unsafe {
            volatile_store(vram() + i, attr + msg[i] as u16)
        }
        i += 1
    }
    // 2. halt forever (until you build the IDT + scheduler)
    halt()
}

func halt() -> never {
    unsafe {
        asm("cli", "")
    }
    halt()
}
```

The repo's `examples/kernel/` is this skeleton, plus a `linker.ld` and build commands.

## The roadmap after hello-world

Each of these is a project of its own (pointers to the classic literature):

| Step | What you build | Where to read |
|---|---|---|
| Boot protocol | multiboot header / UEFI stub | GNU GRUB manual, "osdev.org Bare Bones" |
| GDT + IDT | segment/ interrupt tables | osdev.org "GDT Tutorial", Intel SDM Vol. 3 |
| Serial console | `out`/`in` to port 0x3F8 | osdev.org "Serial Ports" |
| Physical memory | parse multiboot mmap → free list | "The Design and Implementation of the FreeBSD OS" ch. on VM |
| Paging | identity-map kernel, higher half | Intel SDM Vol. 3 ch. 4 |
| Heap kmalloc | buddy/slab over pages | "Solaris Internals" (slab) |
| Scheduler | round-robin over `task` structs | xv6 book (MIT PDOS) |
| Syscalls | `int 0x80` / `syscall` entry | Linux `man 2 syscall` + xv6 |

The xv6 book and osdev.org wiki pair perfectly with Core: every C snippet translates 1:1 — Core structs have C layout ([structs.md](structs.md)), pointers are raw ([pointers.md](pointers.md)), and the unsafe/volatile idioms match ([unsafe.md](unsafe.md), [low-level-programming.md](low-level-programming.md)).

## Core-specific advantages for OS work

- **No runtime to fight.** Freestanding mode drops everything; no GC pauses in your interrupt handlers.
- **C-compatible structs + `@packed`.** Hardware tables (IDT entries, multiboot info, ACPI headers) map directly.
- **LLVM codegen.** You get `rdmsr`-class asm, SIMD for checksums, and a real optimizer — with `core emit-asm` to audit it.
- **The language is small** enough to reason about what the machine is doing — no hidden allocations in your page fault handler.

## Honest scope statement

Core v0.1 gives you: freestanding compilation, custom entry, volatile, inline asm, packed structs, and full pointer control. It does **not** give you: an assembler with full clobber lists ([inline-assembly.md](inline-assembly.md) limitations), a libc replacement, a test harness that runs on hardware (QEMU is your test bench), or debugging beyond `--debug` DWARF (GDB against QEMU's `-s -S` works). Expect to write the unglamorous 90% — boot code, tables, drivers — yourself; that's true in any language, and Core's job is to make the other 10% (logic) pleasant.

## Complete working example

The skeleton above compiles end-to-end with the freestanding flow (verified in [freestanding-development.md](freestanding-development.md)):

```bash
core compile kernel.elf kernel.cr --freestanding --target=x86_64 --emit-object
ld -T linker.ld -o kernel.elf kernel.elf.coreobj.o
qemu-system-x86_64 -kernel kernel.elf   # or your boot protocol of choice
```

`examples/kernel/` in the repo has the exact files; the VGA write logic (put_cell/clear/write above) verifies logically against the framebuffer format — run it and read "Core OS says hello" in white on black.

## Common mistakes

- **Writing to VGA without volatile** — works until the optimizer deletes your stores.
- **Assuming a stack.** Triple fault at `push` — set up `rsp` before any call if your boot path doesn't provide one.
- **Enabling interrupts "to see what happens"** without an IDT — CPU reset loop.
- **Casting framebuffer pointers casually** — use the cell math; `ptr<u16>` at `0xB8000`, not `ptr<u8>` miscounting cells.
- **Testing on real hardware first.** QEMU + `-d int` logs; hardware debugging needs two computers and JTAG.

## Performance notes

- The framebuffer write is a memory bus transaction per cell — scroll by `memcpy`ing rows (pointer loops, [pointers.md](pointers.md)), not per-cell reprints.
- Disable interrupts (`cli`) around multi-instruction hardware sequences you can't tolerate interleaving — then re-enable (`sti`).
- Keep the kernel -Os sized: every KB is boot-time and cache pressure.

## When to use / not use

- **Do this** to understand computers at every layer — it's the best education in systems programming there is.
- **Don't** do this to ship a product OS alone; production kernels are decade-scale team efforts. Use Core freestanding for learning, research kernels, or embedded firmware — and see [low-level-programming.md](low-level-programming.md) for hosted drivers instead.
- If your "OS" is one program with no users or processes — that's freestanding application development; skip the interrupt machinery.


## Exercises

1. Extend the skeleton to paint each of the 25 rows in a different foreground color (loop over rows, attribute math).
2. Write `clear(fg: u8, bg: u8)` from this page and call it before printing — confirm a clean screen.
3. Add `write_at(s: string, row: i32, col: i32, fg: u8, bg: u8)` with a bounds check against COLS/ROWS.
4. Implement scrolling: copy every row's cells up one row with a pointer loop, then blank the last row.
5. Write `kernel_oops(msg: string)` that paints the whole screen red-on-black and prints the message centered — wire it as your freestanding `panic` replacement.

That completes the tutorial series — back to [introduction.md](introduction.md) for the index.
