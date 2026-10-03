// Threads and a mutex-protected counter.
import thread

struct Account {
    balance: i64
    lock: Mutex
    pub func init(v: i64 = 0) { self.balance = v; self.lock.init() }
}

func deposit(acc: ptr<Account>, amount: i64) {
    acc.lock.lock()
    acc.balance += amount
    acc.lock.unlock()
}

func main() {
    acc = Account { }
    acc.init(0)
    pa = &acc // closures capture by value: share through a pointer
    // two workers each deposit 100 times
    w1 = thread.spawn(func() {
        for i in 0..100 { deposit(pa, 1) }
    })
    w2 = thread.spawn(func() {
        for i in 0..100 { deposit(pa, 1) }
    })
    thread.join(w1)
    thread.join(w2)
    say acc.balance // 200
}
