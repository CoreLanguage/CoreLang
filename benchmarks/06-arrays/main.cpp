// C++ has no bounds checks on raw arrays - this is the "unchecked" baseline.
#include <cstdio>
#include <cstdint>

static int64_t data[1000000];
static volatile int64_t base;

int main() {
    base = 3;
    for (int64_t i = 0; i < 1000000; i++) data[i] = i * base;
    int64_t sum = 0;
    for (int64_t rep = 0; rep < 100; rep++) {
        for (int64_t i = 0; i < 1000000; i++) sum += data[i];
        for (int64_t i = 0; i < 1000000; i++) sum += data[(i * 7) % 1000000];
    }
    printf("%lld\n", (long long)sum);
    return 0;
}
