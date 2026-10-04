# librace: data-structure race

Core stdlib-style containers (hand-written in Core) vs equivalent hand-rolled
C++: dynamic array with geometric growth, open-addressing hash map (linear
probing, same hash function), Lomuto quicksort over a raw array, and a
string builder.

Same methodology as the main suite: identical workload, same LLVM backend,
min of 5 runs, **outputs must match exactly** (verified).

```console
$ ./run.sh -O2     # or -O0
```

## Results (this machine, min of 5)

| opt  | Core  | C++   | verdict     |
|------|-------|-------|-------------|
| -O0  | 0.38s | 0.58s | core faster |
| -O2  | 0.23s | 0.28s | core faster |

Core comes out ahead at both opt levels. Nothing exotic is going on: both
sides are honest implementations of the same algorithms, and Core's generic
structs monomorphize to the same code a C++ template would produce. The
C++ side uses std::vector-style doubling for the array, the exact same
open-addressing scheme for the map, and the same Lomuto quicksort.
