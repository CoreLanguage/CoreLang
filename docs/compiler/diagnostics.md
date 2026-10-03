# Diagnostics

The diagnostics engine is small and self-contained: `src/Diag.h` /
`src/Diag.cpp` (~100 lines). It renders errors, warnings, and notes with
source excerpts, caret underlines, and optional `help:` lines.

```cpp
enum class DiagStyle { Error, Warning, Note };

class Diagnostics {
public:
  explicit Diagnostics(SourceMgr &sm) : sm(sm) {}

  void error(SourceLoc loc, const std::string &msg, const std::string &help = "",
             unsigned squiggleLen = 0);
  void warning(SourceLoc loc, const std::string &msg, const std::string &help = "",
               unsigned squiggleLen = 0);
  void note(SourceLoc loc, const std::string &msg);
  void plainError(const std::string &msg); // no source position

  int errorCount = 0;
  int warningCount = 0;
  bool hasErrors() const { return errorCount > 0; }

  SourceMgr &sm;
};
```

## Output format

The doc comment in `Diag.h` is the spec:

```
// Produces richly formatted errors like:
//   error: cannot add `string` and `i32`
//     --> main.cr:12:18
//      12 | say "Age: " + age
//         |             ^~~
```

Concretely, for

```cr
func main() {
    d = Dog { name: "Rex" }
    sp = d as Speakable
}
```

where `Dog` doesn't implement `Speakable`, the compiler prints (real
output; colors when stderr is a TTY):

```
error: class 'Dog' does not implement interface 'Speakable'
   --> /tmp/mondo/shapes.cr:23:10
   23 |     sp = d as Speakable
      |              ^
```

and for a type error with a `help` string:

```
error: cannot assign to immutable variable 'm'
   --> shapes.cr:28:24
   28 |         Circle(r) { m = r }
      |                        ^
   = help: declare it with `mut x = ...`
```

Rendering rules implemented in the static `emit` function:

- `error`/`warning`/`note` tags, colored red/yellow/blue when `isatty(2)`;
  all color codes drop when stderr is not a TTY (so test scripts see clean
  text).
- The `--> file:line:col` header uses the file path from `SourceMgr`.
- The source line comes from `SourceMgr::getLineText` (re-reads the stored
  file text — no file I/O at diagnostic time).
- The caret line indents `col - 1` spaces (plus gutter width), prints `^`,
  and extends with `~` for `squiggleLen - 1` more characters when
  `squiggleLen > 1`.
- If a `help` string was passed, it renders as `<pad> = help: <text>`.
- Invalid locations fall back to a bare `error: <msg>` line.
- `plainError` never touches the source rendering and is used for
  process-level failures (`cannot read source file '...'`, `circular import
  detected: ...`, linking failures).

## How phases use it

- Errors are **counted, not fatal**: `emit` increments `errorCount`, phases
  keep going (with `Type::Invalid` as poison), and the driver checks
  `diag.hasErrors()` at phase boundaries (`registerModules` returns
  `!diag.hasErrors()`, `runPipelineInternal` bails before codegen).
- `Sema::quiet_` (and the parser's own `quiet_`) suppress emission during
  speculative work: `Sema::resolveType` checks `if (quiet_ == 0)` before
  reporting `unknown type`, and overload-probe failures render candidate
  signatures under `quiet_` so only the final error shows.
- The parser funnels everything through `Parser::errorAt`, which drops
  diagnostics while `quiet_ > 0`.

## Writing a good error

The house style, distilled from the existing calls:

1. **Lead with the operation, then the offending types.** Type errors name
   both sides, using `Sema::typeToString`:

   ```cpp
   diag.error(b->loc, strfmt("cannot apply '%s' to '%s' and '%s'", op.c_str(),
                             typeToString(lt).c_str(), typeToString(rt).c_str()),
              "Core does not implicitly mix integer widths or float/integer: use `as` to cast", 1);
   ```

2. **Add a `help` string with the concrete fix** — a snippet the user can
   paste. Existing patterns: `declare it with \`mut x = ...\``,
   `wrap in \`unsafe { ... }\``,
   `add the missing variants or a \`_\` wildcard arm`.
3. **Pass a squiggle length** so the underline covers the token (the
   caller usually has `tk().len` or a name length handy; `1` = single
   caret).
4. **Prefer the earliest meaningful location.** Conditions report at the
   condition's loc, not the `if` keyword; overload failures report at the
   call.

Adding one, end to end:

```cpp
// in the check function, after you have both types:
diag.error(expr->loc,
           strfmt("cannot compare '%s' with '%s'",
                  typeToString(lhs->type).c_str(), typeToString(rhs->type).c_str()),
           dst && dst->isInt() ? "cast one side: `(a as i64) < b`" : "",
           /*squiggleLen*/ 1);
```

Then add a negative test:

```python
expect_compile_error(w, 'func main() {\n    ...\n}\n', "cannot compare")
```

(see [../contributing/testing.md](../contributing/testing.md)).

## Common failure modes

- **Diagnostic text is API.** `tests/run_tests.py` matches `needle in
  stderr` (`"cannot apply '+'"`, `"if condition must be bool"`,
  `"circular import"`...). Rewording an error breaks the suite — update
  the test in the same commit.
- **Emitting during speculation.** Always use `errorAt` (parser) or check
  `quiet_ == 0` (sema); otherwise speculative parses produce bogus errors
  before the good one.
- **`strfmt` is printf.** Passing a user-controlled string (an identifier)
  as the *format* corrupts output; always format
  (`strfmt("unknown name '%s'", name.c_str())`).
- **Colors in captured output.** `emit` checks `isatty(2)`, but if you add
  a new output path, use the same guard or tests will see escape codes.
- **Cascades.** After an error, keep annotating with `tc.invalid()` rather
  than returning early whenever practical — the suite relies on multiple
  independent errors being reported in one compile.
