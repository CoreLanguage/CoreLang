// Explicit allocation: a growable buffer with manual lifetime.
import memory

struct Buffer {
    data: ptr<i32>
    len: usize
    cap: usize
}

func buf_init(b: ptr<Buffer>, cap: usize) {
    b.data = alloc_array<i32>(cap)
    b.len = 0
    b.cap = cap
}

func buf_push(b: ptr<Buffer>, v: i32) {
    if b.len == b.cap {
        b.cap = b.cap * 2
        b.data = realloc_array<i32>(b.data, b.cap)
    }
    b.data[b.len] = v
    b.len += 1
}

func buf_free(b: ptr<Buffer>) {
    free(b.data)
}

func main() {
    mut b: Buffer
    // zero-value struct: give it storage first
    b.data = null
    b.len = 0
    b.cap = 0
    buf_init(&b, 4)
    for i in 0..10 { buf_push(&b, i * i) }
    say b.len
    say b.data[7]
    buf_free(&b)
}
