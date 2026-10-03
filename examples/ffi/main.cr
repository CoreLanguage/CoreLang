// Calling C library functions directly through the C ABI.
extern func printf(fmt: ptr<char>, ...) -> i32
extern func atof(s: ptr<char>) -> f64
extern func abs(x: i32) -> i32

func main() {
    printf("hello from libc: %d %s %.2f\n", 7, c_str("strings cross the ABI"), 1.5)
    say atoi_free()
    say abs(-42)
    say atof("3.5") * 2.0
}

extern func atoi(s: ptr<char>) -> i32

func atoi_free() -> i32 {
    return atoi("1234")
}
