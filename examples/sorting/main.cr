// Quicksort + binary search over a fixed array.
func quicksort(arr: ptr<i32>, lo: i32, hi: i32) {
    if lo >= hi { return }
    mut p = partition(arr, lo, hi)
    quicksort(arr, lo, p - 1)
    quicksort(arr, p + 1, hi)
}

func partition(arr: ptr<i32>, lo: i32, hi: i32) -> i32 {
    pivot = arr[hi]
    mut i = lo - 1
    for mut j in lo..hi {
        if arr[j] < pivot {
            i += 1
            t = arr[i]
            arr[i] = arr[j]
            arr[j] = t
        }
    }
    t = arr[i + 1]
    arr[i + 1] = arr[hi]
    arr[hi] = t
    return i + 1
}

func binary_search(arr: ptr<i32>, n: i32, key: i32) -> i32 {
    mut lo: i32 = 0
    mut hi: i32 = n - 1
    while lo <= hi {
        mut mid = (lo + hi) / 2
        if arr[mid] == key { return mid }
        if arr[mid] < key { lo = mid + 1 }
        else { hi = mid - 1 }
    }
    return -1
}

func main() {
    data: [i32; 10] = [42, 7, 19, 3, 88, 1, 56, 23, 9, 71]
    quicksort(&data, 0, 9)
    for x in data { say x }
    say binary_search(&data, 10, 23)
    say binary_search(&data, 10, 5)
}
