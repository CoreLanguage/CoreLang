// Integer arithmetic: serially-dependent update chain (cannot vectorize -
// each iteration reads the previous result). Tests raw integer codegen.
func main() -> i32 {
    mut sum: u64 = 0
    mut i: u64 = 0
    while i < 400000000 {
        sum = sum * 6364136223846793005 + i + 1442695040888963407
        i += 1
    }
    say sum
    return 0
}
