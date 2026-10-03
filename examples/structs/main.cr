// Structs: predictable C-compatible layout, nesting, arrays.
import math
struct Vec2 {
    x: f64
    y: f64
}

struct Line {
    a: Vec2
    b: Vec2
}

func length(l: Line) -> f64 {
    dx = l.b.x - l.a.x
    dy = l.b.y - l.a.y
    return sqrt(dx * dx + dy * dy)
}

func main() {
    l = Line { a: Vec2 { x: 0.0, y: 0.0 }, b: Vec2 { x: 3.0, y: 4.0 } }
    say length(l)
    l.b.x = 6.0
    say length(l)
}
