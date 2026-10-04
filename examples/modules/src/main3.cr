import libda

func main() {
    a: DynArray<i64> = DynArray<i64> { data: null, len: 0, cap: 0 }
    a.push(10)
    a.push(20)
    a.push(30)
    say a.get(0)
    say a.get(2)
    say a.len
}
