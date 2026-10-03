# Strings

Core's `string` is a **view**: a 16-byte value type `{ptr, len}` pointing at bytes owned elsewhere. This one decision explains every string behavior in the language — cheap passing and slicing, no hidden allocation, and explicit ownership for buffers you create.

## The mental model

```
string = { ptr: ptr<u8>, len: usize }   // 16 bytes total
```

- String **literals** point at static memory in the binary — valid for the program's lifetime.
- `s + t` (concatenation) **allocates** a fresh buffer through the runtime and returns a view over it. That memory is not GC'd; treat concatenated results as heap allocations you may want to copy out of or let live for the program's duration.
- Passing strings to functions copies the 16-byte view, never the bytes.

## Literals and escapes

```core
s = "hello"
line = "first\nsecond"
quoted = "she said \"hi\""
backslash = "C:\\path"
```

| Escape | Meaning |
|---|---|
| `\n` | newline |
| `\t` | tab |
| `\"` | double quote |
| `\\` | backslash |

There are no raw strings or multi-line literals in v0.1 — concatenate with `+`.

## Length, indexing, bytes

- `len(s)` returns the **byte count** (`usize`). Core strings are bytes, not Unicode code points: `len("héllo")` is `6` because `é` is two bytes in UTF-8.
- `s[i]` yields a `char` (one byte) with bounds checking; unchecked inside `unsafe`.
- **Strings are not directly iterable with `for` in v0.1** — loop over indices:

```core
s = "core"
for i in 0..len(s) {
    say s[i]        // c, o, r, e
}
```

## Concatenation: `+` allocates

```core
s = "core"
t = s + "lang"      // new heap buffer, t views it
say t               // corelang
say t == "corelang" // true
```

Building strings with repeated `+` in a loop allocates each time. For hot paths, build into a byte buffer you allocate yourself (see [memory-management.md](memory-management.md)).

## Comparison

`== != < <= > >=` are **lexicographic byte comparisons**:

```core
say "abc" == "abc"    // true
say "abc" < "abd"     // true
say "abc" < "abcd"    // true (prefix is smaller)
say "B" < "a"         // true — bytes: 'B' (66) < 'a' (97)
```

The prelude also offers `str_eq(a, b)` and `str_cmp(a, b) -> i32` (negative/zero/positive like C's `strcmp`).

## `c_str` and FFI

`c_str(s) -> ptr<char>` exposes the byte pointer for C interop; `str_from_c(p)` wraps a NUL-terminated C string back into a view:

```core
extern func printf(fmt: ptr<char>, ...) -> i32

func main() {
    printf("via c_str: %s\n", c_str("works"))
    say str_from_c(c_str("round trip"))   // round trip
}
```

Lifetimes: `c_str` of a **literal** is valid forever; `c_str` of a concatenated string is valid as long as that buffer lives (it's a malloc'd block — the runtime keeps it, it is not collected).

## Strings in structs and collections

Because a `string` is 16 bytes of view, storing it in an array/struct is cheap — but remember it views bytes with their own lifetime. Storing a view of a temporary buffer means the view can outlive the bytes if you free them.

## Complete working example

```core
// strings.cr
extern func printf(fmt: ptr<char>, ...) -> i32

func count_char(s: string, target: char) -> i32 {
    mut n = 0
    for i in 0..len(s) {
        if s[i] == target { n += 1 }
    }
    return n
}

func main() {
    s = "core"
    say len(s)             // 4
    say s[0]               // c
    t = s + "lang"
    say t                  // corelang
    say t == "corelang"    // true
    say s < t              // true
    say "abc" < "abcd"     // true
    say "B" < "a"          // true (byte order)

    say "tab\there"
    say "quote:\" backslash:\\"

    mut last = ' '
    for i in 0..len(t) { last = t[i] }
    say last               // g

    say count_char("mississippi", 's')   // 4

    printf("via c_str: %s\n", c_str("works"))
    p = c_str(s)           // literals are static: pointer stays valid
    say str_from_c(p) == "core"          // true
}
```

## Common mistakes

- **Thinking `string` owns its bytes.** It's a view; concatenation allocates, literals are static.
- **`len()` counting letters.** It counts bytes; multibyte UTF-8 inflates the count.
- **Passing `s` directly to a C variadic call.** `%s` needs `c_str(s)`; a bare `string` view is rejected for the C ABI.
- **Iterating with `for x in s`.** Not supported in v0.1; index with `s[i]`.
- **Repeated `+` in hot loops** — each `+` allocates; batch into a buffer instead.
- **Comparing for content vs identity.** `==` compares **content** (bytes). Two different literals with the same text are `==`.

## Performance notes

- Passing/returning strings copies 16 bytes — effectively free.
- Equality/compare are byte-wise `memcmp`-class operations.
- `+` costs a malloc + two `memcpy`s per call; N concatenations cost N allocations.
- For parsing hot paths, work with `(ptr<u8>, usize)` slices you carve yourself.

## When to use / not use

- Use `string` for text in interfaces, messages, identifiers, config.
- Don't use it as a byte container for binary data — use `ptr<u8>` + `len` or a `[]`-of-bytes struct so ownership is explicit.
- For user-facing programs that build lots of dynamic text, consider wrapping a growable byte buffer with helper functions (see [data-structures.md](data-structures.md)).

## Exercises

1. Count the vowels in a string by indexing (`s[i]`) in a `for i in 0..len(s)` loop.
2. Reverse a string into a second buffer (a `[char; N]` array) and print it back char by char.
3. Write `is_palindrome(s: string) -> bool` comparing `s[i]` and `s[len(s) - 1 - i]`.
4. Write `to_upper(c: char) -> char` for lowercase ASCII letters (`c >= 'a' && c <= 'z'`) and map "core lang" through it.
5. Declare `extern func strlen(s: ptr<char>) -> i32` and compare `strlen(c_str(s))` with `len(s)` for three strings — explain when they can differ (they can't here; bytes vs bytes).

Next: [Structs](structs.md).
