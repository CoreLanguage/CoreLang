// Inline assembly through LLVM's asm integration (x86-64).
func cpuid_max() -> u64 {
    // cpuid with eax=0: highest leaf in ebx... keep it simple: rdtsc tick
    t = unsafe { asm_volatile("rdtsc", "=A") }
    return t
}

func add_one(x: u64) -> u64 {
    // AT&T syntax: add immediate 1 to the register holding x
    return unsafe { asm_volatile("addq $$1, $0", "=r,0", x) }
}

func main() {
    say cpuid_max() != 0
    say add_one(41)
}
