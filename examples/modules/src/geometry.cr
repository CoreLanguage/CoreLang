import algebra

pub struct Vec2 {
    x: f64
    y: f64
}

pub func dist(v: Vec2) -> f64 {
    return algebra.sqrt_v(v.x * v.x + v.y * v.y)
}
