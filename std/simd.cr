// ===================================================================
// Core standard library - SIMD
// Vector primitives lowered directly to LLVM vector operations.
// The compiler recognizes these builtins; `+ - * /` on vector values
// are lane-wise operations.
// ===================================================================

// splat: broadcast a scalar into every lane.
pub func splat(v: f32) -> f32x4 { return splat_f32x4(v) }
pub func splat(v: f64) -> f64x2 { return splat_f64x2(v) }
pub func splat(v: i32) -> i32x4 { return splat_i32x4(v) }
pub func splat(v: i64) -> i64x2 { return splat_i64x2(v) }
pub func splat(v: i8)  -> i8x16 { return splat_i8x16(v) }
pub func splat(v: u8)  -> u8x16 { return splat_u8x16(v) }
pub func splat(v: u32) -> u32x4 { return splat_u32x4(v) }

// extract lane i; replace lane i with x.
pub func extract(v: f32x4, i: u32) -> f32 { return simd_extract_f32x4(v, i) }
pub func extract(v: f64x2, i: u32) -> f64 { return simd_extract_f64x2(v, i) }
pub func extract(v: i32x4, i: u32) -> i32 { return simd_extract_i32x4(v, i) }
pub func extract(v: i64x2, i: u32) -> i64 { return simd_extract_i64x2(v, i) }
pub func extract(v: i8x16, i: u32) -> i8  { return simd_extract_i8x16(v, i) }
pub func replace(v: f32x4, i: u32, x: f32) -> f32x4 { return simd_replace_f32x4(v, i, x) }
pub func replace(v: f64x2, i: u32, x: f64) -> f64x2 { return simd_replace_f64x2(v, i, x) }
pub func replace(v: i32x4, i: u32, x: i32) -> i32x4 { return simd_replace_i32x4(v, i, x) }
pub func replace(v: i64x2, i: u32, x: i64) -> i64x2 { return simd_replace_i64x2(v, i, x) }
pub func replace(v: i8x16, i: u32, x: i8)  -> i8x16  { return simd_replace_i8x16(v, i, x) }

// horizontal sum of all lanes (common reduction)
pub func sum(v: f32x4) -> f32 {
    return extract(v, 0) + extract(v, 1) + extract(v, 2) + extract(v, 3)
}
