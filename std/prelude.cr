// ===================================================================
// Core standard library - prelude
// Automatically imported into every Core module. Keep it small:
// output, string helpers, panics, and the Option/Result enums.
// ===================================================================

// --- runtime entry points (implemented in corert.o, C ABI) ---
extern func core_rt_print_str(s: string) -> void
extern func core_rt_print_nl() -> void
extern func core_rt_print_char(c: char) -> void
extern func core_rt_print_bool(b: bool) -> void
extern func core_rt_print_ptr(p: ptr<void>) -> void
extern func core_rt_print_i8(v: i8) -> void
extern func core_rt_print_i16(v: i16) -> void
extern func core_rt_print_i32(v: i32) -> void
extern func core_rt_print_i64(v: i64) -> void
extern func core_rt_print_u8(v: u8) -> void
extern func core_rt_print_u16(v: u16) -> void
extern func core_rt_print_u32(v: u32) -> void
extern func core_rt_print_u64(v: u64) -> void
extern func core_rt_print_i128(v: i128) -> void
extern func core_rt_print_u128(v: u128) -> void
extern func core_rt_print_f32(v: f32) -> void
extern func core_rt_print_f64(v: f64) -> void
extern func core_rt_str_concat(a: string, b: string) -> string
extern func core_rt_str_eq(a: string, b: string) -> bool
extern func core_rt_str_cmp(a: string, b: string) -> i32
extern func core_rt_str_ptr(s: string) -> ptr<char>
extern func core_rt_str_from_c(p: ptr<char>) -> string
extern func core_rt_panic(msg: string) -> never
extern func core_rt_assert_failed(msg: string, file: string, line: u64) -> void

// --- say: print a value followed by a newline ---
pub func say(v: string) { core_rt_print_str(v); core_rt_print_nl() }
pub func say(v: bool)   { core_rt_print_bool(v); core_rt_print_nl() }
pub func say(v: char)   { core_rt_print_char(v); core_rt_print_nl() }
pub func say(v: i8)     { core_rt_print_i8(v); core_rt_print_nl() }
pub func say(v: i16)    { core_rt_print_i16(v); core_rt_print_nl() }
pub func say(v: i32)    { core_rt_print_i32(v); core_rt_print_nl() }
pub func say(v: i64)    { core_rt_print_i64(v); core_rt_print_nl() }
pub func say(v: i128)   { core_rt_print_i128(v); core_rt_print_nl() }
pub func say(v: u8)     { core_rt_print_u8(v); core_rt_print_nl() }
pub func say(v: u16)    { core_rt_print_u16(v); core_rt_print_nl() }
pub func say(v: u32)    { core_rt_print_u32(v); core_rt_print_nl() }
pub func say(v: u64)    { core_rt_print_u64(v); core_rt_print_nl() }
pub func say(v: u128)   { core_rt_print_u128(v); core_rt_print_nl() }
pub func say(v: usize)  { core_rt_print_u64(v as u64); core_rt_print_nl() }
pub func say(v: isize)  { core_rt_print_i64(v as i64); core_rt_print_nl() }
pub func say(v: f32)    { core_rt_print_f32(v); core_rt_print_nl() }
pub func say(v: f64)    { core_rt_print_f64(v); core_rt_print_nl() }
pub func say<T>(v: ptr<T>) {
    unsafe {
        core_rt_print_ptr(v as ptr<void>)
    }
    core_rt_print_nl()
}

// --- print: without newline ---
pub func print(v: string) { core_rt_print_str(v) }
pub func print(v: i32)    { core_rt_print_i32(v) }
pub func print(v: i64)    { core_rt_print_i64(v) }
pub func print(v: u64)    { core_rt_print_u64(v) }

// --- string helpers ---
// Byte pointer of a string view (for FFI). The pointer stays valid as long
// as the string's memory does; literals are static for the program lifetime.
pub func c_str(s: string) -> ptr<char> { return core_rt_str_ptr(s) }
// Build a string view around a NUL-terminated C string.
pub func str_from_c(p: ptr<char>) -> string { return core_rt_str_from_c(p) }
pub func str_eq(a: string, b: string) -> bool { return core_rt_str_eq(a, b) }
pub func str_cmp(a: string, b: string) -> i32 { return core_rt_str_cmp(a, b) }

// --- panics and assertions ---
// panic stops the program immediately with a message. never functions
// make all code after a call to them unreachable.
pub func panic(msg: string) -> never { core_rt_panic(msg) }

pub func assert(cond: bool) {
    if !cond {
        core_rt_assert_failed("assertion failed", source_file(), source_line())
    }
}
pub func assert(cond: bool, msg: string) {
    if !cond {
        core_rt_assert_failed(msg, source_file(), source_line())
    }
}

// --- common generic enums ---
pub enum Option<T> {
    Some(T),
    None
}

pub enum Result<T, E> {
    Ok(T),
    Err(E)
}
