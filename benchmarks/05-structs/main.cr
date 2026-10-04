// Structs: pass a 64-byte struct by value, copy it, and accumulate fields -
// tests ABI passing, layout, and copy elision.
struct Vec4 {
    a: f64
    b: f64
    c: f64
    d: f64
}

struct Particle {
    pos: Vec4
    vel: Vec4
    id: i64
}

func dot(p: Vec4, q: Vec4) -> f64 {
    return p.a * q.a + p.b * q.b + p.c * q.c + p.d * q.d
}

func advance(p: Particle, dt: f64) -> Particle {
    np = p
    np.pos.a = p.pos.a + p.vel.a * dt
    np.pos.b = p.pos.b + p.vel.b * dt
    np.pos.c = p.pos.c + p.vel.c * dt
    np.pos.d = p.pos.d + p.vel.d * dt
    return np
}

func main() {
    mut p = Particle { pos: Vec4 { a: 1.0, b: 2.0, c: 3.0, d: 4.0 },
                       vel: Vec4 { a: 0.1, b: 0.2, c: 0.3, d: 0.4 }, id: 1 }
    mut acc: f64 = 0.0
    mut i: i64 = 0
    while i < 100000000 {
        p = advance(p, 0.01)
        acc += dot(p.pos, p.vel)
        i += 1
    }
    say acc > 0.0
    say p.id
}
