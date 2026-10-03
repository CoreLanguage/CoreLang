# The parser

`src/Parser.h` / `src/Parser.cpp` implement a hand-written recursive-descent
parser producing the AST defined in `src/AST.h`. The grammar below is the
grammar *as the code implements it* — when this file and `Parser.cpp`
disagree, `Parser.cpp` wins.

```cpp
class Parser {
public:
  Parser(ASTContext &ctx, SourceMgr &sm, unsigned fileID, Diagnostics &diag,
         std::vector<Token> toks)
  // Returns null on unrecoverable error (diagnostics already emitted).
  SourceUnit *parseFile();
```

## Grammar, as implemented

EBNF-ish, with `NL` = at least one newline token:

```
file          := { NL | ";" | attribute | decl } EOF
attribute     := "@" ident [ "(" STRING ")" ]        // applied to next decl
decl          := modifiers ( "func" funcRest | "struct" | "class" | "interface"
               | "trait" | "enum" | "extern" funcRest | constOrGlobal ) NL
modifiers     := { "pub" | "static" | "virtual" | "override" | "abstract"
                 | "unsafe" | "mut" | "tls" }                        // any order
funcRest      := ident [ "<" ident {"," ident} ">" ] "(" params ")" [ "->" type ]
                 ( block | ";" | declBoundary )     // prototype if no body
params        := [ param {"," param} ] ["..."]       // "..." marks variadic
param         := ident ":" type [ "=" expr ]         // default value
struct        := ident [genericParams] [NL] "{" { field | method } "}"
class         := ident [genericParams] [ ":" type {"," type} ] [NL] "{...}"
interface     := ident [genericParams] [NL] "{" "func" funcRest {;|NL} "}"
enum          := ident [genericParams] [NL] "{" variant {","|NL} "}"
variant       := ident [ "(" type {"," type} ")" ]
global        := ["tls"] ["mut"] ident ":" type [ "=" expr ] | ident "=" expr
const         := "const" ident ":" type "=" expr

block         := "{" NL { stmt (NL|";") } "}"
stmt          := "return" [expr] | "break" | "continue" | "if" | "while" | "for"
               | "switch" | "unsafe" | "const" ident ":" type "=" expr
               | letOrExpr
letOrExpr     := ["mut"] ident ":" type ["=" expr]      // declaration
               | ["mut"] ident "=" expr                 // decl OR assign (sema)
               | "say" binary                           // say-sugar
               | assignExpr
for           := [ "mut" ] ident "in" expr [".."|"..=" expr] block   // for-in
               | [letOrExpr] ";" [expr] ";" [letOrExpr] block       // C-style

expr          := assignExpr
assignExpr    := binary [ ("="|"+="|...) binary ]
binary        := unary { op unary }          // precedence climbing, binPrec()
unary         := ("-"|"!"|"~"|"*"|"&") unary | postfix
postfix       := primary { "(" args ")" | "[" expr "]" | "." ident | "as" type }
primary       := INT | FLOAT | CHAR | STRING | "true" | "false" | "null"
               | "self" | "sizeof" "(" type ")" | "alignof" "(" type ")"
               | "func" "(" params ")" ["->" type] block      // lambda
               | "unsafe" block | "match" expr "{" arms "}"
               | identPath ["<" typeArgs ">""] [ structLit | "." variant ... ]
               | "(" expr ")" | "[" elems "]" | "[" expr ";" expr "]"
identPath     := ident { "." ident }
```

The statement and expression grammars are threaded through
`parseStatement` → `parseLetOrExpr` → `parseAssignExpr` → `parseBinary` →
`parseUnary` → `parsePostfix` → `parsePrimary`.

## Operator precedence

`Parser::binPrec` is the single precedence table (shared with error
reporting in Sema):

```cpp
int Parser::binPrec(const std::string &op) {
  if (op == "or" || op == "||") return 1;
  if (op == "and" || op == "&&") return 2;
  if (op == "==" || op == "!=") return 3;
  if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
  if (op == "|") return 5;
  if (op == "^") return 6;
  if (op == "&") return 7;
  if (op == "<<" || op == ">>") return 8;
  if (op == "+" || op == "-") return 9;
  if (op == "*" || op == "/" || op == "%") return 10;
  return 0;
}
```

`parseBinary(minPrec)` is textbook precedence climbing. All binary
operators are left-associative; assignment is handled one level up in
`parseAssignExpr` (right-recursive, via `parseBinary(1)` on the right).

## The say-sugar

`say` is *not* a keyword. `parseLetOrExpr` implements the print statement as
syntactic sugar for a call:

```cpp
// `say expr` is statement-level sugar for `say(expr)`: the print statement.
// It grabs the full expression so `say x + y` prints (x + y).
if (atIdent() && tk().text == "say") {
  const Token &n = tk(1);
  bool special = n.kind == Tok::Punct && (n.text == "." || n.text == "=" || ...);
  bool endsExpr = n.kind == Tok::Newline || n.kind == Tok::EndOfFile ||
                  (n.kind == Tok::Punct && n.text == "}");
  if (!special && !endsExpr) {
    Token sayTok = advance();
    Expr *arg = parseBinary(1);
    ...
    auto *call = ctx.make<ECall>(sayTok.loc, ident, std::vector<Expr *>{arg});
    return ctx.make<SExpr>(l, call);
  }
}
```

Notes:

- The argument is `parseBinary(1)` — the *whole* expression, so
  `say x + y` prints `x + y`, and `say f(1) + g(2)` works.
- If `say` is followed by `.`, `=`, a compound-assignment operator, or a
  `,`, the sugar is skipped and `say` is treated as an ordinary name (so
  `say.f(...)` still parses). A user-defined `say` function therefore
  shadows the sugar in those positions; the prelude's `pub func say(v: ...)`
  overloads do the real work.
- `say` only exists in statement position; `let x = say 5` is a parse error.

## Speculative generic-argument parsing (`quiet_`)

After an identifier, the parser cannot tell whether `<` starts generic
arguments (`Box<i32> { value: 5 }`) or a less-than comparison
(`x < y && ...`). `parsePrimary` resolves this by *speculative parsing*:

```cpp
if (atPunct("<") && !tk().newlineBefore) {
  // try to parse generic args, only committing if followed by '{'
  size_t save = i;
  quiet_++;
  advance();
  skipNewlines();
  bool ok = true;
  while (true) {
    TypeExpr *ga = parseType();
    if (!ga) { ok = false; break; }
    gargs.push_back(ga);
    ...
  }
  quiet_--;
  if (ok && eatPunct(">") &&
      ((atPunct("{") && !tk().newlineBefore) || atPunct(".") ||
       (atPunct("(") && !tk().newlineBefore))) {
    hasGenerics = true;
  } else {
    i = save; // rewind: not a generic struct literal
    gargs.clear();
  }
}
```

Three mechanisms cooperate:

1. **`quiet_`** — an `int` counter; `errorAt` suppresses *all* diagnostics
   while it is positive, so the failed attempt emits nothing.
2. **Checkpoint/rewind** — `save = i` before the attempt, `i = save` on
   failure. Token positions are plain indices into `toks`, so rewinding is
   free.
3. **Commit condition** — the parse only sticks if `>` is followed by `{`
   (struct literal), `.` (`Option<i32>.Some(5)`), or `(` (constructor
   call). Everything else rewinds and re-parses the tokens as comparisons.

The same quiet-rewind pattern reappears in `parseStatement` for `else`
(`save = i` around the newline skip so the separator is restored when there
is no `else`). Note that the parser's `quiet_` silences *parse* errors;
`Sema` has its own separate `quiet_` counter that silences *semantic*
errors during speculative overload checks.

## Statements vs. declarations

- Declarations appear only at the top level (`parseFile` → `parseTopDecl`);
  functions may not be nested. Struct/class/interface/enum bodies contain
  only fields and methods.
- Attributes (`@packed`, `@link_name("...")`; `@inline`/`@repr` accepted as
  no-ops) accumulate in `parseFile` and are consumed by the next
  `parseTopDecl`; an unknown attribute is an error listing the available
  set.
- Statement separation: `parseBlock` requires a newline, `;` or `}` after
  every statement (`expected newline or '}' after statement, found ...`).
  Calls, indexes, member accesses, and `as` casts must not start on a new
  line (the `newlineBefore` guards in `parsePostfix`), so statement
  boundaries are unambiguous without semicolons.
- `parseLetOrExpr` handles Core's Go-like `x = 10`: the parser cannot know
  whether `x` already exists, so it produces an `SLet` with
  `isDeclOrAssign = true` and lets `Sema::checkStmt` decide (assign to
  existing, or declare new) — see [semantic.md](semantic.md).

## Parsing types

`parseType` builds a syntactic `TypeExpr` (resolved later by
`Sema::resolveType`):

- Keywords that name primitives map via `primKindByName` to
  `TypeExpr::Prim`.
- `func(i32, i32) -> i32` is a `TypeExpr::Func` (function *type*, used for
  lambdas and closure values).
- `[elem; size]` is `TypeExpr::Array`; the size is a full expression,
  const-folded later by `Sema::evalConstUint`.
- Dotted names (`math.Vec`) and generic arguments (`ptr<T>`, `Box<i32>`)
  are `TypeExpr::Named` with `nameParts`/`genericArgs`. `ptr` is *not* a
  keyword — it is resolved by name in `Sema::resolveNamedType`.

## Common failure modes

- **Forgetting `!tk().newlineBefore`** on a new postfix form: statements
  will "swallow" the next line.
- **Committing a speculative parse too eagerly.** The commit condition after
  `>` must list every legal continuation; today that is `{`, `.` and `(`.
  Generic syntax in new positions (e.g. a cast `as Box<T>`) needs its own
  lookahead decision, not a reuse of this one.
- **Emitting diagnostics from speculative code.** Anything reachable during
  a speculative attempt must go through `errorAt` (which checks `quiet_`),
  never `diag.error` directly.
- **Prototypes.** `parseFuncRest` treats a declaration as body-less when the
  next token is `;`, a newline, or any declaration starter (`extern`, `pub`,
  `func`, `struct`, `class`, `interface`, `trait`, `enum`, `import`, `@`,
  `abstract`, `virtual`, `override`). A new top-level declaration keyword
  must be added to that boundary check, or a prototype followed by the new
  form will try to parse the next declaration as a body.
