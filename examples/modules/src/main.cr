// Multi-module project: main imports geometry, geometry imports algebra.
// Build from the example root with: core compile geometry_demo src/main.cr
import geometry

func main() {
    say geometry.dist(geometry.Vec2 { x: 3.0, y: 4.0 })
}
