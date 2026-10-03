// Lock-free counting with atomics.
import thread

func main() {
    counter = AtomicI32 { }
    counter.init(0)
    pc = &counter
    t1 = thread.spawn(func() { for i in 0..50000 { pc.add(1) } })
    t2 = thread.spawn(func() { for i in 0..50000 { pc.add(1) } })
    thread.join(t1)
    thread.join(t2)
    say counter.load()

    // compare-and-swap loop
    expected = counter.swap(0)
    say expected
}
