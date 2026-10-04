# Core vs C++ benchmarks

Equivalent programs in both languages, compiled with the same LLVM 18
backend, timed on the same machine. **Outputs must match exactly** or the
result is discarded (the harness verifies this).

## Methodology

- Each benchmark does real work for ~0.1–0.5 s so the 10 ms timer
  resolution is irrelevant.
- Opaque (volatile) inputs where needed so neither compiler can
  constant-fold the workload away.
- 5 runs per benchmark; the **minimum** is reported (min = least noise).
- Compile: `core ... -O2` vs `g++ -O2` (same machine, same LLVM codegen).

Run it yourself:

```console
$ ./run.sh -O2        # or -O0
```

## Results (this machine: 2-core VM, clang/g++ 13, LLVM 18)

| benchmark                     | Core  | C++   | verdict                          |
|-------------------------------|-------|-------|----------------------------------|
| 01 int (serial dep chain)     | 0.43s | 0.43s | even                             |
| 02 float (FP MADD chain)      | 0.32s | 0.32s | even                             |
| 03 loops (prime counting)     | 0.11s | 0.11s | even                             |
| 04 calls (direct+virtual+fn)  | 0.13s | 0.05s | **C++ ~2.6x** (see below)        |
| 05 structs (byval+copies)     | 0.12s | 0.12s | even                             |
| 06 arrays (unchecked)         | 0.11s | 0.10s | even                             |
| 06 arrays (bounds-checked)    | 0.11s | 0.10s | even (checks elided/hoisted)     |

## Honest analysis

- **int / float / loops / structs / arrays: dead even.** Expected and
  correct: both languages lower to LLVM IR and hand the same optimizer the
  same problem. Core is *not* faster than C++ and does not try to be.
- **The one measured gap is 04-calls, and it is worth understanding.**
  C++ finished 300M iterations in 0.05s because its optimizer
  *devirtualized* `b->step(x)` — the receiver's dynamic type is provably
  `Impl`, so the virtual call became a direct call and then folded. Core's
  virtual calls always load the vtable pointer and call through it
  (~1 ns/call). Core prefers static dispatch on concrete-typed receivers,
  so the guidance is: **hot loops on concrete objects are already direct;
  expect real dispatch cost only when you call through a base pointer or
  interface on purpose.** (A future class-hierarchy analysis could close
  this; see docs/contributing/adding-a-feature.md → "add an optimization".)
- **Bounds checks are nearly free** in the sequential-array benchmark:
  LLVM hoists/eliminates the loop-invariant `i < 1000000` checks. For
  unpredictable access patterns the checks cost more — that is what
  `unsafe { }` is for (see docs/language/memory-model.md).

Do not trust micro-benchmarks — including these. Run your own workload.
