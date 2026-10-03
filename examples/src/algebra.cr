pub func sqrt_v(x: f64) -> f64 {
    // Newton's method - self-contained so the example needs no native libs
    mut guess = x
    for i in 0..24 {
        guess = (guess + x / guess) / 2.0
    }
    return guess
}
