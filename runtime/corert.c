/* Core runtime support library (corert).
 *
 * A minimal, explicit, no-GC runtime used by Core's standard library.
 * This file is compiled once into corert.o and linked into every hosted
 * Core program. Freestanding (kernel) builds do NOT link this file.
 *
 * Everything here is a thin, predictable wrapper over the C library.
 * There is no garbage collector, no background thread, no hidden allocation
 * except where a function's contract says so (e.g. string concatenation).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/* Core's `string` value: a view {pointer, byte length}. */
typedef struct { const char *ptr; unsigned long long len; } CRString;

static inline CRString cr_str(const char *p, unsigned long long len) {
  CRString s; s.ptr = p; s.len = len; return s;
}

/* ------------------------------------------------------------- printing -- */
static void cr_print_bytes(const char *p, unsigned long long n) {
  if (n) fwrite(p, 1, (size_t)n, stdout);
}
void core_rt_print_str(CRString s)  { cr_print_bytes(s.ptr, s.len); }
void core_rt_print_nl(void)         { fputc('\n', stdout); }
void core_rt_print_char(uint32_t c) { fputc((char)c, stdout); }
void core_rt_print_bool(int b)      { fputs(b ? "true" : "false", stdout); }
void core_rt_print_ptr(void *p)     { printf("%p", p); }

void core_rt_print_i8(int8_t v)   { printf("%lld", (long long)v); }
void core_rt_print_i16(int16_t v) { printf("%lld", (long long)v); }
void core_rt_print_i32(int32_t v) { printf("%lld", (long long)v); }
void core_rt_print_i64(int64_t v) { printf("%lld", (long long)v); }
void core_rt_print_u8(uint8_t v)  { printf("%llu", (unsigned long long)v); }
void core_rt_print_u16(uint16_t v){ printf("%llu", (unsigned long long)v); }
void core_rt_print_u32(uint32_t v){ printf("%llu", (unsigned long long)v); }
void core_rt_print_u64(uint64_t v){ printf("%llu", (unsigned long long)v); }
void core_rt_print_f32(float v)   { printf("%.9g", (double)v); }
void core_rt_print_f64(double v)  { printf("%.17g", v); }

/* 128-bit integer printing (no printf support on most libcs). */
void core_rt_print_u128(unsigned __int128 v) {
  char buf[64];
  int i = 0;
  if (v == 0) { fputs("0", stdout); return; }
  while (v > 0) {
    unsigned __int128 q = v / 10;
    buf[i++] = (char)('0' + (int)(v - q * 10));
    v = q;
  }
  while (i > 0) fputc(buf[--i], stdout);
}
void core_rt_print_i128(__int128 v) {
  if (v < 0) { fputc('-', stdout); core_rt_print_u128((unsigned __int128)(-v)); }
  else core_rt_print_u128((unsigned __int128)v);
}

/* --------------------------------------------------------------- strings -- */
CRString core_rt_str_concat(CRString a, CRString b) {
  unsigned long long n = a.len + b.len;
  char *buf = (char *)malloc(n ? (size_t)n : 1);
  if (!buf) { fputs("panic: out of memory\n", stderr); abort(); }
  memcpy(buf, a.ptr, (size_t)a.len);
  memcpy(buf + a.len, b.ptr, (size_t)b.len);
  return cr_str(buf, n);
}
int core_rt_str_eq(CRString a, CRString b) {
  if (a.len != b.len) return 0;
  return a.len == 0 || memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}
int core_rt_str_cmp(CRString a, CRString b) {
  unsigned long long n = a.len < b.len ? a.len : b.len;
  int r = n ? memcmp(a.ptr, b.ptr, (size_t)n) : 0;
  if (r) return r < 0 ? -1 : 1;
  if (a.len == b.len) return 0;
  return a.len < b.len ? -1 : 1;
}
const char *core_rt_str_ptr(CRString s) { return s.ptr; }
CRString core_rt_str_from_c(const char *p) {
  return cr_str(p, p ? strlen(p) : 0);
}
CRString core_rt_i64_to_string(long long v) {
  char buf[32];
  int n = snprintf(buf, sizeof buf, "%lld", v);
  return core_rt_str_concat(cr_str(buf, (unsigned)n), cr_str("", 0));
}

/* --------------------------------------------------------------- memory -- */
void *core_rt_alloc(unsigned long long size) {
  void *p = malloc((size_t)(size ? size : 1));
  if (!p) { fputs("panic: out of memory\n", stderr); abort(); }
  return p;
}
void *core_rt_alloc_zero(unsigned long long size) {
  void *p = calloc(1, (size_t)(size ? size : 1));
  if (!p) { fputs("panic: out of memory\n", stderr); abort(); }
  return p;
}
void *core_rt_alloc_aligned(unsigned long long size, unsigned long long align) {
  if (align == 0) align = 1;
  void *p = NULL;
  int rc = posix_memalign(&p, (size_t)align, (size_t)(size ? size : 1));
  if (rc != 0 || !p) { fputs("panic: out of memory (aligned)\n", stderr); abort(); }
  return p;
}
void *core_rt_realloc(void *p, unsigned long long size) {
  void *q = realloc(p, (size_t)(size ? size : 1));
  if (!q) { fputs("panic: out of memory (realloc)\n", stderr); abort(); }
  return q;
}
void core_rt_free(void *p) { free(p); }
void core_rt_memcpy(void *dst, const void *src, unsigned long long n) { memcpy(dst, src, (size_t)n); }
void core_rt_memmove(void *dst, const void *src, unsigned long long n) { memmove(dst, src, (size_t)n); }
void core_rt_memset(void *dst, int byte, unsigned long long n) { memset(dst, byte, (size_t)n); }
int core_rt_memcmp(const void *a, const void *b, unsigned long long n) { return memcmp(a, b, (size_t)n); }

/* ------------------------------------------------------- panics/asserts -- */
void core_rt_panic(CRString msg) {
  fputs("panic: ", stderr);
  fwrite(msg.ptr, 1, (size_t)msg.len, stderr);
  fputc('\n', stderr);
  abort();
}
void core_rt_assert_failed(CRString msg, CRString file, unsigned long long line) {
  fputs("assertion failed: ", stderr);
  fwrite(msg.ptr, 1, (size_t)msg.len, stderr);
  fprintf(stderr, " (%.*s:%llu)\n", (int)file.len, file.ptr, line);
  abort();
}
void core_rt_index_failed(CRString file, unsigned long long line, long long index, unsigned long long len) {
  fprintf(stderr, "panic: index %lld out of bounds for length %llu (%.*s:%llu)\n",
          index, len, (int)file.len, file.ptr, line);
  abort();
}
void core_rt_exit(int code) { exit(code); }

/* ----------------------------------------------------------------- time -- */
unsigned long long core_rt_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL;
}
unsigned long long core_rt_monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL;
}
void core_rt_sleep_ms(unsigned long long ms) {
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000ULL);
  ts.tv_nsec = (long)(ms % 1000ULL) * 1000000L;
  nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------- threads -- */
typedef void (*cr_closure_fn)(void *);

typedef struct {
  cr_closure_fn fn;
  void *env;
} CRThreadStart;

static void *cr_thread_trampoline(void *arg) {
  CRThreadStart *s = (CRThreadStart *)arg;
  cr_closure_fn fn = s->fn;
  void *env = s->env;
  free(s);
  fn(env);
  return NULL;
}

/* Returns malloc'd pthread_t handle. */
void *core_rt_thread_spawn(cr_closure_fn fn, void *env) {
  pthread_t *t = (pthread_t *)malloc(sizeof(pthread_t));
  if (!t) { fputs("panic: out of memory\n", stderr); abort(); }
  CRThreadStart *s = (CRThreadStart *)malloc(sizeof(CRThreadStart));
  if (!s) { fputs("panic: out of memory\n", stderr); abort(); }
  s->fn = fn;
  s->env = env;
  if (pthread_create(t, NULL, cr_thread_trampoline, s) != 0) {
    fputs("panic: failed to spawn thread\n", stderr);
    abort();
  }
  return t;
}
void core_rt_thread_join(void *handle) { pthread_join(*(pthread_t *)handle, NULL); free(handle); }

void *core_rt_mutex_new(void) {
  pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  if (!m) { fputs("panic: out of memory\n", stderr); abort(); }
  pthread_mutex_init(m, NULL);
  return m;
}
void core_rt_mutex_lock(void *m)    { pthread_mutex_lock((pthread_mutex_t *)m); }
void core_rt_mutex_unlock(void *m)  { pthread_mutex_unlock((pthread_mutex_t *)m); }
void core_rt_mutex_free(void *m)    { pthread_mutex_destroy((pthread_mutex_t *)m); free(m); }

void *core_rt_rwlock_new(void) {
  pthread_rwlock_t *m = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
  if (!m) { fputs("panic: out of memory\n", stderr); abort(); }
  pthread_rwlock_init(m, NULL);
  return m;
}
void core_rt_rwlock_read(void *m)    { pthread_rwlock_rdlock((pthread_rwlock_t *)m); }
void core_rt_rwlock_write(void *m)   { pthread_rwlock_wrlock((pthread_rwlock_t *)m); }
void core_rt_rwlock_unlock(void *m)  { pthread_rwlock_unlock((pthread_rwlock_t *)m); }
void core_rt_rwlock_free(void *m)    { pthread_rwlock_destroy((pthread_rwlock_t *)m); free(m); }

void *core_rt_cond_new(void) {
  pthread_cond_t *c = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  if (!c) { fputs("panic: out of memory\n", stderr); abort(); }
  pthread_cond_init(c, NULL);
  return c;
}
void core_rt_cond_wait(void *c, void *m) { pthread_cond_wait((pthread_cond_t *)c, (pthread_mutex_t *)m); }
void core_rt_cond_signal(void *c)        { pthread_cond_signal((pthread_cond_t *)c); }
void core_rt_cond_broadcast(void *c)     { pthread_cond_broadcast((pthread_cond_t *)c); }
void core_rt_cond_free(void *c)          { pthread_cond_destroy((pthread_cond_t *)c); free(c); }

/* -------------------------------------------------------- process args -- */
extern char **environ;
int core_rt_argc(void) { return 0; } /* replaced below via constructor trick */
static int cr_argc_real = 0;
static char **cr_argv_real = NULL;
int cr_argc_impl(void);

__attribute__((constructor)) static void cr_save_args(void) {
  /* glibc does not expose argv to constructors portably; use /proc-less way:
     main_wrapper below is preferred. For simplicity we parse /proc/self/cmdline. */
}
int core_rt_arg_count(void) {
  if (cr_argv_real) return cr_argc_real;
  /* fallback: read /proc/self/cmdline */
  FILE *f = fopen("/proc/self/cmdline", "rb");
  if (!f) return 0;
  int n = 0;
  int c;
  while ((c = fgetc(f)) != EOF) {
    if (c == 0) n++;
  }
  fclose(f);
  return n;
}
CRString core_rt_arg(int i) {
  if (!cr_argv_real) {
    /* re-read /proc/self/cmdline */
    static char buf[4096];
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f) return cr_str("", 0);
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = 0;
    int idx = 0;
    char *p = buf;
    while ((size_t)(p - buf) < got) {
      if (idx == i) return cr_str(p, strlen(p));
      p += strlen(p) + 1;
      idx++;
    }
    return cr_str("", 0);
  }
  if (i < 0 || i >= cr_argc_real) return cr_str("", 0);
  return cr_str(cr_argv_real[i], strlen(cr_argv_real[i]));
}
/* The codegen'd main wrapper calls this so argv is captured reliably. */
void core_rt_save_args(int argc, char **argv) {
  cr_argc_real = argc;
  cr_argv_real = argv;
}
