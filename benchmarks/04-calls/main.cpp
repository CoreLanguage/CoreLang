#include <cstdio>
#include <cstdint>

static int64_t add_small(int64_t a, int64_t b) { return a + b; }

struct Base {
    virtual int64_t step(int64_t x) { return x + 1; }
    virtual ~Base() = default;
};
struct Impl : Base {
    int64_t step(int64_t x) override { return x + 1; }
};

static volatile int64_t ctrl; // opaque to the optimizer

int main() {
    ctrl = 1;
    int64_t x = 0;
    for (int64_t i = 0; i < 100000000LL; i++) x = add_small(x, ctrl);
    printf("%lld\n", (long long)x);

    Impl obj;
    Base* b = &obj;
    x = 0;
    for (int64_t i = 0; i < 100000000LL; i++) x = b->step(x);
    printf("%lld\n", (long long)x);

    int64_t (*f)(int64_t, int64_t) = [](int64_t a, int64_t b) { return a + b; };
    x = 0;
    for (int64_t i = 0; i < 100000000LL; i++) x = f(x, ctrl);
    printf("%lld\n", (long long)x);
    return 0;
}
