// A tiny calculator exercising functions and control flow.
func apply(op: char, a: f64, b: f64) -> f64 {
    if op == '+' { return a + b }
    if op == '-' { return a - b }
    if op == '*' { return a * b }
    if op == '/' { return a / b }
    return 0.0
}

func main() {
    x = 12.0
    y = 4.0
    ops: [char; 4] = ['+', '-', '*', '/']
    for op in ops {
        say apply(op, x, y)
    }
}
