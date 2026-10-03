// ===================================================================
// Core standard library - process
// ===================================================================

extern func core_rt_exit(code: i32) -> never
extern func core_rt_arg_count() -> i32
extern func core_rt_arg(i: i32) -> string

// Terminate the process immediately with the given exit code.
pub func exit(code: i32) -> never { core_rt_exit(code) }

// Number of command line arguments, including the program name.
pub func arg_count() -> i32 { return core_rt_arg_count() }
// Argument i (0 = program name), or an empty string when out of range.
pub func arg(i: i32) -> string { return core_rt_arg(i) }
