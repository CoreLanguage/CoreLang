#include <cstdio>
#include <cstdint>
int main() {
    double acc = 0.5;
    uint64_t i = 0;
    while (i < 200000000ULL) {
        acc = acc * 1.0000000001 + 0.000000001;
        i += 1;
    }
    printf("%s\n", acc > 0.0 ? "true" : "false");
    return 0;
}
