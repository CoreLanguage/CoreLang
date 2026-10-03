# Core Language - Ground Truth Summary (for documentation writers)

This file describes EXACTLY what the Core compiler implements today.
Every claim here is verified by tests/ and examples/. Do not document
features that are not in this file as if they work.

## Compilation model
- `core` is a native C++ compiler using LLVM 18 as the backend.
- Pipeline: lex -> parse -> AST -> semantic analysis (name resolution,
  type checking, monomorphization of generics) -> LLVM IR -> LLVM
  optimization passes -> native object code -> system linker (cc) ->
  ONE native executable.
- Files use the .cr extension. Every imported .cr file is compiled into the
  same final executable (whole-program compilation). Binaries do not need
  the sources at runtime.
- Commands: core init [name], core compile <output> <entry.cr>, core build,
  core run, core test, core check, core install <pkg>, core remove <pkg>,
  core update, core list, core emit-ir <f>, core emit-asm <f>, core version.
- compile flags: -O0/-O1/-O2/-O3/-Os, --debug (DWARF debug info; binaries
  are GDB-usable, breakpoints resolve to .cr lines), --target=x86_64|aarch64|
  riscv64 (emits a relocatable object; linking needs a target toolchain),
  --freestanding (no runtime, no libc; @link_name("_start") entry; --emit-object),
  --emit-object, --force, --link=<lib>, --link-arg=<arg>, --lib-path=<dir>.

## Syntax overview
- Newline-terminated statements. Braces for blocks. Semicolons optional.
- Comments: // line, /* block */.
- Variables: `x = 10` (immutable, type inferred; assigns if x already exists
  in scope, else declares - Go-like), `mut x: i32 = 10`, `x: i32 = 10`,
  `const MAX: i32 = 100` (compile-time constant).
- Globals: `name: type = const-expr` (const-foldable initializers only),
  `mut name: type = ...`, `tls name: type = ...` (thread-local).
- Functions: `func add(a: i32, b: i32) -> i32 { return a + b }`.
  Overloads allowed (must be unambiguous). Default arguments supported.
  `extern func puts(s: ptr<char>) -> i32` declares C functions (C ABI,
  no mangling; variadic via `...`). Function-local functions: no.
  Entry: `func main()` (void or i32).
- Falling off the end: numeric/bool/char/pointer/string/enum functions
  implicitly return the default value (0, 0.0, false, null, "", variant 0).
  Aggregate (struct/class/array) returns REQUIRE an explicit return.
  `never` return type: function must not return normally; calls to never
  functions make following code unreachable.
- Control flow: if/else (conditions must be bool - no truthiness),
  while, `for i in 0..n` (exclusive range), `for i in 0..=n` (inclusive),
  `for x in array`, C-style `for i = 0; i < n; i += 1`, break, continue,
  switch (case values: integers/chars/enums; no fallthrough),
  match (patterns: enum variants with payload binding `Some(v)`,
  literals, bindings, `_` wildcard; exhaustive for enums).
- `unsafe { ... }` blocks: required for pointer-to-pointer casts with
  different pointee types, int<->ptr casts, volatile ops, inline asm,
  and class downcasts/interface unwraps. Works as a statement AND as an
  expression (value = last expression in the block).

## Types
- Primitives: void, never, bool, char (8-bit), string,
  i8 i16 i32 i64 i128, u8 u16 u32 u64 u128, f32 f64, usize isize (64-bit),
  SIMD vectors: f32x4 f64x2 i32x4 i64x2 i8x16 i16x8 u8x16 u16x8 u32x4 u64x2.
- No implicit numeric conversions (not even i32->i64). Literals fit their
  context: integer literals convert to any integer type in range; float
  literals to f32/f64. Constant-foldable args (e.g. `4 * 10`) convert too.
- `string` is a value type: a view {ptr, len} over bytes (8 bytes + 8).
  Literals are static. `+` concatenates (allocates via the runtime).
  Comparisons == != < <= > >= are lexicographic byte comparisons.
  Indexing s[i] yields char, with bounds checking (unchecked in unsafe).
- ptr<T>: raw pointer. ptr<void> is the void/opaque pointer (i8*).
  &x takes addresses, *p dereferences (allowed anywhere, documented).
  Pointer arithmetic p + i / p - i scales by the pointee size.
  p - q yields isize element distance. == != < <= > >= compare pointers.
- Arrays: `[i32; 5]` fixed-size value types. `arr[i]` bounds-checked
  (unchecked inside unsafe). `len(arr)` builtin. Array literals `[1, 2, 3]`
  and repeat form `[0; 10]`. ptr<[T; N]> implicitly converts to ptr<T>
  in argument position (decay).
- sizeof(T) / alignof(T): compile-time, target-true values.

## User-defined types
- struct: C-compatible layout (natural alignment), @packed attribute for
  packed layout. Fields + methods (static dispatch). All fields required
  in literals unless the type has an init constructor.
- class: single inheritance `class Dog : Animal`, interfaces/traits after
  a comma. Fields + methods. Polymorphic classes (with virtual/abstract
  methods, or deriving from polymorphic ones) carry a vtable pointer at
  offset 0. `virtual func`, `override func`, `abstract func` (abstract
  classes can't be instantiated). Static methods: `static func`.
  Access: `pub` makes members public; class members are private by default,
  struct members public by default.
- Constructors: `func init(...)` methods; the class literal `Dog { }` calls
  the no-arg-capable init (all params defaulted); init can call the base
  via `Animal.init("dog")`. init methods store the class vtable on every
  exit (the most-derived init's store wins).
- interface: pure method contracts. trait: interface with default method
  bodies. `class C : SomeInterface, SomeTrait` implements them.
  Interface VALUES are fat pointers {object ptr, itable ptr} created via
  `obj as Interface`; method calls go through itables.
- enum: simple enums (`enum Color { Red, Green }` - i32 backed) and
  data-carrying enums (`enum Shape { Circle(f64), Rect(f64, f64), Point }`
  - tagged unions). Construct: `Color.Red`, `Shape.Circle(2.0)` or with
  generics `Option<i32>.Some(5)`. Match extracts payloads.
- Generics: `func id<T>(x: T) -> T`, `struct Box<T>`, generic classes.
  Monomorphized at compile time (mangled like `_C4math3maxIi32E`).
  Explicit args at call sites: `alloc_array<i32>(5)`. Type inference
  unifies argument types. No trait bounds in v1.

## Operators
- Arithmetic + - * / %, bitwise & | ^ ~ << >>, comparisons == != < <= > >=,
  logical && || ! (and/or words too), unary -, address-of &, deref *,
  assignment = += -= *= /= %= &= |= ^= <<= >>=, cast `as`.
- `as` casts: int<->int, int<->float, bool<->int, enum<->int always OK.
  ptr<->ptr with different pointees, int<->ptr, class downcasts, interface
  unwraps: unsafe-only. string -> ptr<char> implicit in calls (FFI).

## Modules & packages
- `import math`, `import utils.helper` (binds `helper`), `import x as y`.
- Resolution order: directory of the importing file, project src/, installed
  dependency packages, std library. `pub` marks decls visible cross-module.
- Project: core.toml manifest, core.lock lockfile, src/ layout.
  `core init [name]` scaffolds (refuses to overwrite).
- Packages: git-based. `core install <repo-or-path>` clones into
  ~/.cache/core/git, resolves semver tags (^X.Y.Z constraints), records
  exact version + commit in core.lock, updates core.toml.

## Standard library (std/, auto-imported prelude + explicit modules)
- prelude (auto): say overloads for every primitive, print, assert, panic,
  c_str/str_from_c/str_eq/str_cmp, Option<T>, Result<T, E>.
- memory: alloc<T>, alloc_zeroed<T>, alloc_array<T>, alloc_zeroed_array<T>,
  alloc_bytes, alloc_aligned, realloc_array, free, memcpy/memset/memcmp.
- math: PI, E, sqrt, pow, sin, cos, abs, floor, ceil, min/max/clamp<T>.
- thread: spawn/join (OS threads, pthread-based), Thread/Mutex/RwLock/Cond
  classes, AtomicI32/AtomicI64/AtomicBool/AtomicUsize (seq_cst),
  atomic_load/store/add/sub/swap/cas/fence builtins.
- time: time_ms, monotonic_ms, sleep_ms. process: exit, arg_count, arg.
- simd: splat/extract/replace per vector type, sum; +-*/ on vectors are
  lane-wise; lowered to LLVM vector IR.

## Builtins (compiler-implemented, shadowable by user functions)
len(x), sizeof(T), alignof(T), source_file(), source_line(),
volatile_load(ptr)/volatile_store(ptr, v) [unsafe],
asm(asmstr, constraints, args...) / asm_volatile [unsafe] -> u64
(LLVM inline-asm syntax, AT&T style on x86).

## Runtime (runtime/corert.c)
A thin C library: printing, string ops, malloc wrappers, panic/assert
reporting, pthread wrappers, time. NO garbage collector, NO hidden
allocation, NO background threads. Freestanding builds do not link it.

## Known limitations (v1 - document honestly)
- No closures capturing by reference: lambdas capture enclosing locals BY
  VALUE. Share mutable state through captured pointers to heap memory.
- No if-expressions (match works as an expression).
- Whole-program compilation (no incremental/separate object caching).
- Cross builds stop at object files (no target linkers bundled).
- Generic interfaces not supported yet.
- main() reads argv via /proc/self/cmdline (Linux).
- No string iteration in for-in yet.
