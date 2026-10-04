// librace driver (C++ implementation) - identical workload to core/main.cr
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <algorithm>

// ---- dynarray: geometric growth (equivalent of core/dynarray.cr) ----
static void dynarray() {
    std::vector<int64_t> v;
    v.reserve(0);
    for (int64_t i = 0; i < 10000000; i++) v.push_back(i);
    int64_t sum = 0;
    for (size_t i = 0; i < v.size(); i++) sum += v[i];
    printf("%lld\n", (long long)sum);
}

// ---- hashmap: open addressing, linear probing, power-of-two cap ----
struct HM {
    std::vector<int64_t> keys, vals;
    size_t cap = 0, size = 0;
    static constexpr int64_t EMPTY = INT64_MIN;
    HM() {}
    void grow() {
        size_t nc = cap ? cap * 2 : 16;
        std::vector<int64_t> nk(nc, EMPTY), nv(nc, 0);
        for (size_t i = 0; i < cap; i++)
            if (keys[i] != EMPTY) {
                size_t idx = hash(keys[i]) & (nc - 1);
                while (nk[idx] != EMPTY) idx = (idx + 1) & (nc - 1);
                nk[idx] = keys[i]; nv[idx] = vals[i];
            }
        keys = std::move(nk); vals = std::move(nv); cap = nc;
    }
    static size_t hash(int64_t k) {
        uint64_t h = (uint64_t)k;
        h *= 0x9E3779B97F4A7C15ULL;
        h ^= h >> 32;
        return (size_t)h;
    }
    void put(int64_t k, int64_t v) {
        if (size * 10 >= cap * 7) grow();
        size_t idx = hash(k) & (cap - 1);
        while (keys[idx] != EMPTY) {
            if (keys[idx] == k) { vals[idx] = v; return; }
            idx = (idx + 1) & (cap - 1);
        }
        keys[idx] = k; vals[idx] = v; size++;
    }
    int64_t get(int64_t k) const {
        if (!cap) return -1;
        size_t idx = hash(k) & (cap - 1);
        while (keys[idx] != EMPTY) {
            if (keys[idx] == k) return vals[idx];
            idx = (idx + 1) & (cap - 1);
        }
        return -1;
    }
};
static void hashmap() {
    HM m;
    for (int64_t i = 0; i < 1000000; i++) m.put(i, i * 7);
    int64_t lsum = 0;
    for (int64_t i = 0; i < 1000000; i++) {
        int64_t got = m.get(i);
        if (got >= 0) lsum += got;
    }
    printf("%lld\n", (long long)lsum);
}

// ---- sort: quicksort Lomuto over raw array ----
static void quicksort(int64_t *a, int64_t lo, int64_t hi) {
    if (lo >= hi) return;
    int64_t pivot = a[hi];
    int64_t i = lo - 1, j = lo;
    while (j < hi) {
        if (a[j] < pivot) { i++; int64_t t = a[i]; a[i] = a[j]; a[j] = t; }
        j++;
    }
    int64_t t = a[i + 1]; a[i + 1] = a[hi]; a[hi] = t;
    quicksort(a, lo, i);
    quicksort(a, i + 2, hi);
}
static void sortbench() {
    int64_t *arr = (int64_t *)malloc(2000000 * sizeof(int64_t));
    int64_t seed = 12345;
    for (int64_t i = 0; i < 2000000; i++) {
        seed = seed * 1103515245 + 12345;
        arr[i] = seed;
    }
    quicksort(arr, 0, 1999999);
    printf("%lld\n%lld\n", (long long)arr[0], (long long)arr[1999999]);
    free(arr);
}

// ---- strbuf: append loop ----
static void strbuf() {
    // grow-by-doubling char buffer, appending "item-12345;" 100k times
    size_t cap = 64, len = 0;
    char *data = (char *)malloc(cap);
    const char *item = "item-12345;";
    size_t ilen = strlen(item);
    for (int i = 0; i < 100000; i++) {
        while (len + ilen > cap) { cap *= 2; data = (char *)realloc(data, cap); }
        memcpy(data + len, item, ilen);
        len += ilen;
    }
    printf("%zu\n", len);
    free(data);
}

int main() {
    dynarray();
    hashmap();
    sortbench();
    strbuf();
    return 0;
}
