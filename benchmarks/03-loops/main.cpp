#include <cstdio>
int main() {
    int count = 0;
    for (int n = 2; n < 1000000; n++) {
        bool isprime = true;
        for (int d = 2; d * d <= n; d++) {
            if (n % d == 0) { isprime = false; break; }
        }
        if (isprime) count++;
    }
    printf("%d\n", count);
    return 0;
}
