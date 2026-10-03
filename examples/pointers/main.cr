// Raw pointers: addresses, arithmetic, dereferencing, pointer-to-pointer.
func main() {
    x = 10
    p = &x
    say *p        // 10
    *p = 20
    say x         // 20

    // pointer arithmetic walks element by element
    data: [i32; 4] = [10, 20, 30, 40]
    mut q = &data[0]
    say *(q + 2)  // 30
    q += 1
    say *q        // 20
    say q - &data[0]  // element distance: 1

    // pointer to pointer
    pp = &p
    say **pp

    // null and comparison
    n: ptr<i32> = null
    say n == null
    say p != null
    say p == &x // p holds the address of x

    // pointers differ from the data they point at
}
