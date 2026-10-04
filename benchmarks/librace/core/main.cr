// librace driver (Core implementation) - identical workload to cpp/main.cpp
import dynarray
import hashmap
import sort
import strbuf
import memory

func main() {
    // 1. dynarray: 10M pushes with geometric growth
    mut v: DynArray<i64>
    v.init()
    for i in 0..10000000 { v.push(i as i64) }
    mut sum: i64 = 0
    for i in 0..v.size() { sum += v.get(i as usize) }
    say sum

    // 2. hashmap: 1M inserts + 1M lookups
    mut m: HashMap
    m.init()
    for i in 0..1000000 { m.put(i as i64, (i * 7) as i64) }
    mut lsum: i64 = 0
    for i in 0..1000000 {
        got = m.get(i as i64)
        if got >= 0 { lsum += got }
    }
    say lsum

    // 3. sort: 2M pseudo-random i64
    arr = memory.alloc_array<i64>(2000000)
    mut seed: i64 = 12345
    for i in 0..2000000 {
        seed = seed * 1103515245 + 12345
        arr[i] = seed
    }
    quicksort(arr, 0, 1999999)
    say arr[0]
    say arr[1999999]
    memory.free(arr)

    // 4. strbuf: 100k appends
    mut sb: StrBuf
    sb.init()
    for i in 0..100000 { sb.append("item-12345;") }
    say sb.size()
}
