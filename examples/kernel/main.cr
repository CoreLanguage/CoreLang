// Freestanding kernel entry example. Build with:
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
