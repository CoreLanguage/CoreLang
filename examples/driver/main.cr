// MMIO-style device access: a model UART register block, the way drivers
// talk to hardware. Volatile accesses, packed layout, device memory.
@packed struct UartRegs {
    data: u32       // offset 0: data register
    status: u32     // offset 4: status flags
    baud: u32       // offset 8: divisor
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

func main() {
    // simulated register block on the heap (real drivers use device memory)
    regs: ptr<UartRegs> = unsafe {
        core_rt_alloc_aligned(sizeof(UartRegs), 4) as ptr<UartRegs>
    }
    unsafe {
        volatile_store(&regs.baud, 115200)
        volatile_store(&regs.status, 1) // simulate: transmitter ready
    }
    puts_str(regs, "driver online")
    say "uart simulated"
}
