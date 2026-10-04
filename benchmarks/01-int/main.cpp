// Same workload in C++.
#include <cstdio>
#include <cstdint>
int main() {
    uint64_t sum = 0, i = 0;
    while (i < 400000000ULL) {
        sum = sum * 6364136223846793005ULL + i + 1442695040888963407ULL;
        i += 1;
    }
    printf("%llu\n", (unsigned long long)sum);
    return 0;
}
