// libcore: quicksort (Lomuto) over a raw array
pub func quicksort(a: ptr<i64>, lo: i64, hi: i64) {
    if lo >= hi { return }
    p = partition(a, lo, hi)
    quicksort(a, lo, p - 1)
    quicksort(a, p + 1, hi)
}

func partition(a: ptr<i64>, lo: i64, hi: i64) -> i64 {
    pivot = a[hi]
    mut i = lo - 1
    mut j = lo
    while j < hi {
        if a[j] < pivot {
            i += 1
            t = a[i]
            a[i] = a[j]
            a[j] = t
        }
        j += 1
    }
    t = a[i + 1]
    a[i + 1] = a[hi]
    a[hi] = t
    return i + 1
}
