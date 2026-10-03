# The lexer

`src/Lexer.h` / `src/Lexer.cpp` turn `.cr` source text into a flat
`std::vector<Token>`. There is no parser feedback loop: the lexer is a
single pass over the file (`Lexer::tokenizeAll`), and **newline tokens are
significant** because Core uses newline-terminated statements.

## Token representation

```cpp
enum class Tok {
  EndOfFile, Newline, Ident, Kw, IntLit, FloatLit, CharLit, StrLit,
  Punct, Error,
};

struct Token {
  Tok kind = Tok::EndOfFile;
  SourceLoc loc;
  std::string text;      // ident name / keyword / punct / decoded string
  unsigned long long intValue = 0;
  bool big128 = false;   // integer literal exceeds 64 bits
  double floatValue = 0.0;
  unsigned len = 0;      // source length (for caret squiggles)
  bool newlineBefore = false;
};
```

`text` doubles as the payload for four kinds: identifier name, keyword
spelling, punctuator spelling, and the *decoded* bytes of a string literal.
`loc`/`len` exist purely so diagnostics can underline the exact source
range (see [diagnostics.md](diagnostics.md)).

## Newline significance

`tokenizeAll` emits a `Tok::Newline` token at most once per run of newline
characters, and records `newlineBefore` on the next real token:

```cpp
if (c == '\n') {
  if (!out.empty() && out.back().kind != Tok::Newline) {
    Token t;
    t.kind = Tok::Newline;
    ...
  }
  advance();
  continue;
}
```

Two consumers rely on this:

- `Parser::parseBlock` treats a `Newline` token as the statement separator.
- `Parser::parseBinary`/`parsePostfix` check `tk().newlineBefore` so a
  binary operator or `(` at the start of a line never continues the previous
  expression:

  ```cpp
  // operator on its own line does not continue the expression
  if (tk().newlineBefore) break;
  ```

The end-of-file token sets `newlineBefore = true`, so a trailing expression
without a final newline still parses as a complete statement.

## Keywords

`keywordTable` is a `nullptr`-terminated array; `isKeyword()` does a linear
scan. Keywords include declaration words (`func`, `struct`, `class`,
`interface`, `trait`, `enum`, `import`, `extern`), modifiers (`pub`,
`private`, `static`, `mut`, `const`, `abstract`, `virtual`, `override`,
`tls`), control flow (`if`, `else`, `while`, `for`, `in`, `break`,
`continue`, `return`, `switch`, `case`, `default`, `match`), misc (`as`,
`unsafe`, `true`, `false`, `null`, `self`, `super`, `and`, `or`, `sizeof`,
`alignof`) — and **every primitive type name**:

```cpp
// Keywords recognised by the lexer. Primitive type names are keywords so they
// can never be shadowed by user variables.
extern const char *const keywordTable[];
```

That is why `i32` cannot be a variable name: the lexer classifies it as
`Tok::Kw`, and `Parser::expectIdent` reports
`'i32' is a keyword and cannot be used as a name`.

## Number lexing

`Lexer::lexNumber` handles, in order:

1. `0x`/`0X` hex, `0b`/`0B` binary, `0o`/`0O` octal — digits plus `_`
   separators.
2. Decimal: digits and `_`, then optionally `.` **only if followed by a
   digit** (`1.foo` is not a float), then optionally an `e`/`E` exponent
   with optional sign.

The raw text is re-scanned after the fact: `_` separators are stripped,
`strtoull` parses the value with base 16/2/8/10, and `strtod` handles
floats. If the digit string is longer than 16 characters the token sets
`big128 = true`; `Sema::checkExpr` then types such literals as `u128`, and
`Codegen::evalConst` builds the `APInt` from the digit string rather than
the 64-bit value.

## String and char literals

String literals may not span lines (a newline before the closing quote is
an `unterminated string literal` error). `decodeEscape` implements the
escape set `\n \t \r \0 \\ \" \'` and `\xHH` (exactly two hex digits,
otherwise `invalid \x escape in literal`). Unknown escapes produce:

```
error: unknown escape sequence '\q'
  --> main.cr:2:12
   2 | say "a\qb"
   |            ^~
   = help: valid escapes: \n \t \r \0 \\ \" \' \xHH
```

Char literals (`'a'`, `'\n'`, `'\x41'`) store their byte value in
`intValue`; only one character (or one escape) is accepted between the
quotes.

## Comments and punctuation

- `//` to end of line; `/* ... */` with an `unterminated block comment`
  error if the file ends first. Block comments do **not** nest.
- Punctuators are matched longest-first from static tables:

  ```cpp
  static const char *punct3[] = {"<<=", ">>=", "...", nullptr};
  static const char *punct2[] = {"==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
                                 "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                 "..", "->", nullptr};
  ```

  `..=` is special-cased *between* the two tables (a 3-character match that
  is not in `punct3` because `<<=`/`>>=`/`...` are matched first):

  ```cpp
  // careful: "..=" should be matched before ".." and "="
  if (c == '.' && peek() == '.' && peek(2) == '=') { ... }
  ```

  Anything else becomes a single-character `Tok::Punct`.

## Adding a token / keyword

1. Add the spelling to `keywordTable[]` in `Lexer.cpp` (if it is a keyword).
2. If you need a new *kind* (not just a new keyword), add it to `enum Tok`
   and give it a name in `tokName()` — that string appears in parse errors
   ("expected ..., found *kind*").
3. Punctuators: add to `punct2`/`punct3` (order within a table does not
   matter; length classes do) and check that nothing lexes it earlier.
4. Then update the parser — see [parser.md](parser.md) and
   [../contributing/adding-a-feature.md](../contributing/adding-a-feature.md)
   for the full pipeline walkthrough.

## Common failure modes

- **`1..5` lexing.** `lexNumber` requires a digit after `.` for floats, so
  `1..5` lexes as `IntLit(1)`, `Punct("..")`, `IntLit(5)`. But `1.` followed
  by a non-digit is two tokens (`1` then `.`) — do not "fix" the number
  lexer to consume a bare trailing dot.
- **Keywords as identifiers.** Adding a common word (e.g. `type`) to
  `keywordTable` breaks every program using it as a variable. Keywords are
  reserved forever; prefer context-sensitive parsing over new keywords.
- **Forgetting `newlineBefore`.** Any new prefix/postfix form in the parser
  must check `tk().newlineBefore`, or `x\n(ARGS)` becomes a call across
  lines.
- **big128 literals.** A 20-digit literal lexes fine but is only usable as
  `u128`/`i128`; `Sema::checkExpr` gives it `PRIM_u128`, and `evalConst`
  parses `digits`, not `intValue` (which is truncated). If you add a new
  consumer of `EInt`, handle `big128` explicitly.
