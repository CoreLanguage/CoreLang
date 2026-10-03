// ===================================================================
// Core standard library - math
// ===================================================================

pub const PI: f64 = 3.14159265358979323846
pub const E: f64 = 2.71828182845904523536

extern func core_rt_sqrt(x: f64) -> f64
extern func core_rt_pow(x: f64, y: f64) -> f64
extern func core_rt_sin(x: f64) -> f64
extern func core_rt_cos(x: f64) -> f64
extern func core_rt_fabs(x: f64) -> f64
extern func core_rt_floor(x: f64) -> f64
extern func core_rt_ceil(x: f64) -> f64

pub func sqrt(x: f64) -> f64 { return core_rt_sqrt(x) }
pub func pow(x: f64, y: f64) -> f64 { return core_rt_pow(x, y) }
pub func sin(x: f64) -> f64 { return core_rt_sin(x) }
pub func cos(x: f64) -> f64 { return core_rt_cos(x) }
pub func abs(x: f64) -> f64 { return core_rt_fabs(x) }
pub func floor(x: f64) -> f64 { return core_rt_floor(x) }
pub func ceil(x: f64) -> f64 { return core_rt_ceil(x) }

pub func abs(x: i32) -> i32 { if x < 0 { return -x }; return x }
pub func abs(x: i64) -> i64 { if x < 0 { return -x }; return x }

pub func min<T>(a: T, b: T) -> T {
    if a < b { return a }
    return b
}
pub func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}
pub func clamp<T>(v: T, lo: T, hi: T) -> T {
    if v < lo { return lo }
    if v > hi { return hi }
    return v
}
