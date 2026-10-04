// tinylib driver (C++) - identical workload to core/main.cr
#include <cstdio>
#include <cstdint>
#include <cstdlib>

// ---- MinHeap: array-backed binary min-heap of i64 ----
struct MinHeap {
    int64_t *data = nullptr;
    size_t size = 0, cap = 0;
    void push(int64_t v) {
        if (size == cap) {
            size_t nc = cap ? cap * 2 : 16;
            data = (int64_t *)realloc(data, nc * sizeof(int64_t));
            cap = nc;
        }
        data[size] = v;
        size++;
        size_t i = size - 1;
        while (i > 0) {
            size_t parent = (i - 1) / 2;
            if (data[parent] <= data[i]) break;
            int64_t t = data[parent]; data[parent] = data[i]; data[i] = t;
            i = parent;
        }
    }
    int64_t pop() {
        int64_t top = data[0];
        size--;
        if (size > 0) {
            data[0] = data[size];
            size_t i = 0;
            for (;;) {
                size_t l = i * 2 + 1, r = l + 1, smallest = i;
                if (l < size && data[l] < data[smallest]) smallest = l;
                if (r < size && data[r] < data[smallest]) smallest = r;
                if (smallest == i) break;
                int64_t t = data[i]; data[i] = data[smallest]; data[smallest] = t;
                i = smallest;
            }
        }
        return top;
    }
};

// ---- Mat: dense f64 row-major ----
struct Mat {
    double *d;
    size_t n;
    void init(size_t n_) {
        d = (double *)malloc(n_ * n_ * sizeof(double));
        n = n_;
        for (size_t i = 0; i < n * n; i++) d[i] = 0.0;
    }
    void set(size_t r, size_t c, double v) { d[r * n + c] = v; }
    void mul(const Mat &b, Mat &out) const {
        size_t n = this->n;
        for (size_t i = 0; i < n; i++)
            for (size_t k = 0; k < n; k++) {
                double aik = d[i * n + k];
                if (aik != 0.0)
                    for (size_t j = 0; j < n; j++) out.d[i * n + j] += aik * b.d[k * n + j];
            }
    }
    double diag_sum() const {
        double s = 0;
        for (size_t i = 0; i < n; i++) s += d[i * n + i];
        return s;
    }
};

// ---- ASCII <-> i64 ----
static int64_t parse_buf(const char *p, size_t n) {
    int64_t v = 0;
    for (size_t i = 0; i < n; i++) v = v * 10 + (p[i] - '0');
    return v;
}
static size_t format_buf(char *p, int64_t v) {
    if (v == 0) { p[0] = '0'; return 1; }
    size_t cnt = 0;
    for (int64_t mm = v; mm > 0; mm /= 10) cnt++;
    size_t pos = cnt;
    for (int64_t m = v; m > 0; m /= 10) {
        pos--;
        p[pos] = (char)('0' + m % 10);
    }
    return pos == 0 ? cnt : cnt; // digits written at [0, cnt)
}
// wrapper matching the Core control flow exactly (v assumed >= 0)
static size_t fmt(char *p, int64_t v) {
    if (v == 0) { p[0] = '0'; return 1; }
    size_t pos = 0;
    // write backwards from a scratch of 24 like Core does via count-then-fill
    int64_t m = v;
    int digits[24], cnt = 0;
    while (m > 0) { digits[cnt++] = (int)(m % 10); m /= 10; }
    for (int k = cnt - 1; k >= 0; k--) *p++ = (char)('0' + digits[k]);
    return (size_t)cnt;
}

int main() {
    // 1. heap
    MinHeap h;
    int64_t seed = 12345;
    for (int64_t i = 0; i < 1000000; i++) {
        seed = seed * 1103515245 + 12345;
        h.push(seed);
    }
    int64_t sum = 0;
    for (int64_t i = 0; i < 1000000; i++) sum += h.pop();
    printf("%lld\n", (long long)sum);

    // 2. matrix
    const size_t n = 160;
    Mat a, b, c;
    a.d = (double *)calloc(n * n, sizeof(double)); a.n = n;
    b.d = (double *)malloc(n * n * sizeof(double)); b.n = n;
    c.d = (double *)malloc(n * n * sizeof(double)); c.n = n;
    for (size_t i = 0; i < n * n; i++) { a.d[i] = 0.0; b.d[i] = 0.0; c.d[i] = 0.0; }
    seed = 9876;
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            seed = seed * 1103515245 + 12345;
            a.d[i * n + j] = (double)(seed % 1000) / 1000.0;
            seed = seed * 1103515245 + 12345;
            b.d[i * n + j] = (double)(seed % 100) / 100.0;
        }
    // out = a * b, with Core's same loop order and zero-skip
    for (size_t i = 0; i < n; i++)
        for (size_t k = 0; k < n; k++) {
            double aik = a.d[i * n + k];
            if (aik != 0.0)
                for (size_t j = 0; j < n; j++) c.d[i * n + j] += aik * b.d[k * n + j];
        }
    double ds = 0;
    for (size_t i = 0; i < n; i++) ds += c.d[i * n + i];
    printf("%lld\n", (long long)ds);

    // 3. parse
    char buf[24];
    int64_t total = 0;
    seed = 7;
    for (int64_t i = 0; i < 2000000; i++) {
        seed = seed * 1103515245 + 12345;
        int64_t x = seed % 1000000000;
        if (x < 0) x = -x;
        // format
        char tmp[24];
        int nd = 0;
        int64_t m = x;
        if (m == 0) tmp[nd++] = '0';
        while (m > 0) { tmp[nd++] = (char)('0' + m % 10); m /= 10; }
        for (int k = 0; k < nd; k++) buf[k] = tmp[nd - 1 - k];
        int64_t v = 0;
        for (int k = 0; k < nd; k++) v = v * 10 + (buf[k] - '0');
        total += v;
    }
    printf("%lld\n", (long long)total);
    return 0;
}
