# The lexer

How the lexer works today. This page describes the code as it is, and is updated as the lexer
grows. The rules it implements are in [../spec/lexical.md](../spec/lexical.md), and the remaining
work is in [../plan/lexer.md](../plan/lexer.md).

**Status:** parts 0 to 9 of the plan: operators, whitespace, line counting, names, keywords,
stray characters, the error type, all numbers, short comments, and short strings with every
escape. Long brackets (part 10) are not lexed yet.

## Where it lives

| File                            | Contents                                         |
| ------------------------------- | ------------------------------------------------ |
| `lib/internal/lexer/token.hpp`  | `TokenKind`, `TokenValue`, `Token`, `keywords`   |
| `lib/internal/lexer/lexer.hpp`  | the `Lexer` class                                |
| `lib/lexer/lexer.cpp`           | its implementation                               |
| `lib/internal/syntax_error.hpp` | `SyntaxError`, `near_eof`, `near_char`, `near_text` |
| `lib/syntax_error.cpp`          | their implementation                             |
| `tests/lexer_test.cpp`          | lexer tests, tagged `[lexer]`                    |
| `tests/syntax_error_test.cpp`   | error tests, tagged `[syntax-error]`             |

The lexer is internal to the library: embedders never see tokens.

## What it does

The lexer turns source text into tokens, one per call:

```cpp
lua2::Lexer lexer{"local x = y", "test.lua"};  // the text and name must outlive the lexer
std::expected<lua2::Token, lua2::SyntaxError> result = lexer.next();
if (!result) {
    // result.error().message is "test.lua:1: ... near ..."
}
// *result is Local, then Name("x"), Assign, Name("y"), EndOfStream
```

The second argument is the **chunk name** that error messages start with (`test.lua:3: ...`).
It defaults to `"?"`, which is what Lua uses for a chunk with no name.

`next()` returns a `std::expected`: either the next token, or the `SyntaxError` that stopped
lexing. Nothing is ever thrown. At the end of the input it returns `EndOfStream`, and keeps
returning it on every later call.

The lexer hands these tokens to the parser, which checks the grammar and builds the syntax tree.
The lexer itself never decides whether the code is valid Lua.

## Tokens

A `Token` has three fields:

- `kind`: a `TokenKind`, an `enum class` with one entry per kind of token.
- `line`: the line the token **starts** on, counted from 1.
- `value`: a `TokenValue`, a `std::variant` of `std::monostate`, `std::string`, `std::int64_t`
  and `double`. A `Name` holds its text as a `std::string`, and an `Other` holds its one byte as
  a `std::string`. An `Integer` holds a `std::int64_t`, a `Float` a `double`, and a `String` its
  bytes after escapes as a `std::string`. Operators and keywords hold `std::monostate` (no
  value).

Tokens compare with `==` (kind, line and value), which keeps tests short.

`TokenKind` currently has:

- `EndOfStream`
- `Name`
- `Other`, for a stray byte such as `@`
- `Integer` and `Float`, for numerals
- `String`, for string literals
- 23 single-character tokens: `+ - * / % ^ # & ~ | < > = ( ) { } [ ] ; : , .`
- 10 multi-character tokens: `== ~= <= >= << >> // :: .. ...`
- 22 keywords, one kind each: `And`, `Break`, ... `While` (`ElseIf` for `elseif`)

Operator kinds are named after the character, not its meaning (`Minus`, not `Subtract`), because
some characters mean two things: `-` is subtraction and negation, `~` is xor and bitwise not.

## The keyword table

`token.hpp` also holds `keywords`, the spelling of each keyword next to its kind:

```cpp
struct Keyword {
    std::string_view text;
    TokenKind kind;
};

inline constexpr auto keywords = std::to_array<Keyword>({
    {.text = "and", .kind = TokenKind::And},
    // ... one entry per keyword ...
});
static_assert(keywords.size() == 22);
```

It is a `constexpr std::array` and not a `std::map` or `std::vector`. The 22 entries never
change, so the table is built at compile time, lives in read-only memory and costs nothing at
startup. `std::to_array` works out the size from the entries, and the `static_assert` fails the
build if a keyword is added or lost.

## Lexer state

The `Lexer` holds only five things:

- `source_`: a `std::string_view` of the text. The lexer does not copy it, so the caller keeps it
  alive. The constructor's `[[clang::lifetimebound]]` makes clang warn about obvious mistakes,
  such as passing a temporary `std::string`.
- `chunk_name_`: a `std::string_view` of the chunk name, also not copied.
- `pos_`: the index of the next character to read.
- `line_`: the current line.
- `failed_`: a `std::optional<SyntaxError>`, set once an error stops lexing.

## How `next()` works

`next()` is a loop around a `switch` on the current character:

1. At the end of the input, return `EndOfStream`.
2. **Whitespace** (space, `\t`, `\v`, `\f`): skip it and loop again.
3. **Line breaks** (`\n`, `\r`): call `read_newline()` and loop again.
4. **`-`**: a second `-` starts a comment, so call `skip_comment()` and loop again. Otherwise it
   is `Minus`.
5. **A quote** (`"` or `'`): call `read_string()`, which returns a `String`.
6. **Characters that are always one token** (`+ * % ^ # & | ( ) { } [ ] ; ,`): return that
   token.
7. **Characters that may start a longer token** (`= ~ / : < > .`): look ahead with `peek()` and
   take the **longest** token that matches (*maximal munch*). So `==` is one token, `<<=` is `<<`
   then `=`, and `....` is `...` then `.`. The one exception: a `.` followed by a digit starts a
   number, as in `.5`.
8. **A letter or `_`**: call `read_name()`, which returns a name or a keyword.
9. **A digit**: call `read_number()`, which returns an `Integer` or a `Float`.
10. **Any other byte** (`@`, `$`, `!`, a byte above 127, even `\0`) becomes an `Other` token
   holding that byte. See "Stray characters" below.

Private helpers:

| Helper              | What it does                                                     |
| ------------------- | ---------------------------------------------------------------- |
| `make(kind)`        | builds a token of `kind` on the current line, with no value      |
| `take(length, kind)`| builds the token, **then** moves past `length` characters        |
| `single(kind)`      | `take(1, kind)`                                                  |
| `peek(offset)`      | the character `offset` places ahead, or `'\0'` past the end      |
| `read_newline()`    | moves past one line break and counts the line                    |
| `read_name()`       | reads a name, then checks the keyword table                      |
| `read_number()`     | reads a numeral greedily, then validates and converts it         |
| `skip_comment()`    | moves past `--` and the rest of the line, not the line break     |
| `read_string()`     | reads a short string, processing escapes                         |
| `read_escape(text)` | reads one escape and appends its bytes to `text`                 |
| `escape_error(...)` | an error inside an escape, with Lua's near text                  |
| `error(msg, near)`  | records a `SyntaxError` on the current line and returns it       |

`take` builds the token before moving forward, which is how every token gets the line it starts on.
`peek` never reads past the end of the text, so the lookahead cases need no bounds checks.

## Names and keywords

`read_name()` works in two steps:

1. **Read the whole name.** Keep moving forward while the character is a letter, a digit or `_`,
   then take the text with `source_.substr(start, length)`. Taking *everything* is maximal munch
   again: `whilex` is one name, not `while` followed by `x`.
2. **Look it up.** `std::ranges::find(keywords, text, &Keyword::text)` searches the table,
   comparing `text` with each entry's `text` field. On a match the token gets that keyword's kind
   and no value. Otherwise it is a `Name` holding a copy of the text.

So keywords are exact, case-sensitive matches: `while` is `While`, but `While`, `WHILE`, `whilex`
and `_while` are all names. Names like `_ENV` and `_G` are ordinary names to the lexer.

"Letter" means only ASCII `a-z`, `A-Z` and `_`, and "digit" only `0-9`. `is_letter` and `is_digit`
in `lexer.cpp` check these ranges by hand instead of calling `std::isalpha`, because `std::isalpha`
depends on the C locale, and Lua never treats bytes 128-255 as letters.

## Stray characters

A byte that cannot start any token, such as `@`, is **not** a lexer error. The lexer returns it
as an `Other` token and keeps going, so `a@b` is `Name`, `Other`, `Name`. The parser will reject
it later with `unexpected symbol near '@'`. This matches Lua, whose lexer also passes such bytes
through and leaves the complaint to the parser.

Bytes above 127 are never letters, so the two bytes of a UTF-8 `é` are two `Other` tokens.

## Errors

Errors are **returned, never thrown**. `next()` returns `std::expected<Token, SyntaxError>`, so
every place an error can come from or travel through is visible in the code. (This is a project
decision, recorded in [../ROADMAP.md](../ROADMAP.md).)

`SyntaxError` (`lib/internal/syntax_error.hpp`) is a plain struct:

| Field     | Contents                                                              |
| --------- | --------------------------------------------------------------------- |
| `message` | the message Lua prints: `<chunk>:<line>: <message> near <near>`       |
| `line`    | the line of the error                                                 |
| `origin`  | a `std::source_location`: the function, file and line of **our** C++ that created the error |

```
test.lua:3: unfinished string near '"abc'
```

`origin` is for debugging the interpreter, not for Lua users: when an error looks wrong, it says
which line of the lexer produced it. `make_syntax_error(chunk, line, message, near)` builds the
struct, and its last parameter defaults to `std::source_location::current()`, which captures the
caller automatically. `SyntaxError` sits outside `lexer/` because the parser will produce it too.

The `near` part says where the error is, and is passed in already formatted:

- `near_eof` is `<eof>`, with no quotes, for errors at the end of the input.
- `near_char(byte)` quotes a single byte: `'@'` for printable ASCII (space to `~`), and the
  decimal value otherwise, as in `'<\128>'`. It checks the range by hand rather than calling
  `std::isprint`, for the same locale reason as `is_letter`.
- `near_text(text)` quotes the text read so far, for a name, number or string: `'1e+'`.

Inside the lexer, `error(message, near)` builds the `SyntaxError` with the chunk name, the current
line and the caller's `origin`, and returns it as a `std::unexpected`, so a lexing function
reports an error with:

```cpp
return error("unfinished string", near_eof);
```

**One error at a time:** `error()` also stores the error in `failed_`. From then on, `next()`
returns that same error on every call: lexing is over. Like Lua, the lexer never tries to recover
and find more errors. (Reporting all errors is a possible later mode, stage 15 of the roadmap.)

The errors so far are `malformed number`, `unfinished string`, and the escape errors listed under
"Escapes".

## Numbers

`read_number()` handles decimal and hex numerals in two steps, the same way Lua's own `llex.c`
does: **read greedily first, validate second.**

**1. Read greedily.** Take every character that *could* belong to a numeral, without checking
the shape yet:

- If the numeral starts with `0x` or `0X`, it is hex: take the prefix, and the exponent letters
  are `p`/`P`. Otherwise the exponent letters are `e`/`E`.
- Then repeat: an exponent letter (and a `+` or `-` right after it), a hex digit, or a `.`.
  Hex digits are taken even in a decimal numeral, so `3a` is read as one piece.
- Finally, if a letter (or `_`) touches the numeral, take that one too.

So `3x`, `3..2`, `1.2.3` and `0xg` are each read as **one** piece, and then fail as one
malformed number instead of quietly splitting into a number and something else. A `-` belongs to
the numeral only right after an exponent letter, so `1e-5` is one number but `1-5` is three
tokens.

**2. Validate and convert.** Two helpers, `to_integer` and `to_float`, each either accept the
**whole** text or return nothing (`std::optional`):

- `to_integer` handles:
  - **Hex** (`0xff`): at least one hex digit and nothing else. The value is built in a
    `std::uint64_t`, which wraps around modulo 2^64, then converted to `std::int64_t`, which keeps
    the bits: `0xffffffffffffffff` is `-1`.
  - **Decimal** (`345`): `std::from_chars` must use the whole text. A decimal integer too large
    for 64 bits fails here, so it becomes a float, as Lua says.
- `to_float` uses `std::from_chars`, in hex format (without the `0x`) or general format:
  `0xA23p-4` is 162.1875 and `314.16e-2` is 3.1416. A value too large is infinity (`1e400`), and
  a value too small is 0 (`1e-400`). That is not an error in Lua. `from_chars` reports those
  cases as `errc::result_out_of_range`, but libc++ still stores infinity or 0, so that code is
  accepted. Tests pin this down in case a library ever behaves differently.

`read_number()` tries `to_integer`, then `to_float`. If neither accepts the text, the result is
`malformed number near '<text>'`. `std::from_chars` ignores the locale, never allocates, and
reports problems as an error code rather than an exception.

An integer is a numeral with no point and no exponent that fits; anything else that converts is a
`Float`. In hex, `e` is a digit, so `0x1e` is the integer 30.

Numerals have no sign: `-5` is `Minus` then `Integer 5`. The parser turns that into a negation.
This is why `-9223372036854775808` is minus a *float*: the integer part alone is too large.

## Comments

`--` starts a comment, so the `-` case in `next()` peeks at the next character: one `-` is
`Minus`, two start a comment. `skip_comment()` moves past the `--` and everything up to the end of
the line, then `next()` loops again as if it had seen whitespace.

The line break itself is **not** part of the comment: `next()` reads it on its next turn and
counts the line, so line numbers stay right after a comment. A comment may also run to the end of
the input. Inside a comment nothing is lexed, so `-- "unfinished 3x @` produces no tokens and no
error.

Long comments (`--[[ ... ]]`) are not handled yet: until long brackets arrive, they are read as
short comments that end at the end of the line.

## Strings

`read_string()` reads a string in `"..."` or `'...'`. The opening quote decides which quote
closes it, so the other one may appear inside unescaped: `"it's"`, `'say "hi"'`.

It builds the string's value byte by byte in a `std::string`:

- **The closing quote** ends the string.
- **A raw line break** is an error: a short string must stay on one line (`unfinished string`).
- **End of input** is an error too (`unfinished string near <eof>`).
- **A backslash** starts an escape. `read_escape()` reads it and appends the bytes it stands for
  (see the next section).
- **Any other byte** is added as it is, including `\0` and bytes above 127.

The token's line is the line the string **starts** on, even when an escaped line break moves the
lexer to the next line.

Like Lua, the value starts out with the opening quote in it, because error messages show the
string read so far, *after* escapes: for `"a\tb` followed by a line break the message is
`unfinished string near '"a<TAB>b'`. The quote is removed before the token is returned.

## Escapes

`read_escape(text)` starts at the backslash, reads one escape, and appends its bytes to `text`.
It returns `std::expected<void, SyntaxError>`: nothing on success, or the error.

| Escape                  | Adds                                                              |
| ----------------------- | ----------------------------------------------------------------- |
| `\a \b \f \n \r \t \v`  | bell, backspace, form feed, newline, carriage return, tab, vertical tab |
| `\\ \" \'`              | the backslash or quote itself                                     |
| `\` + a real line break | one `\n`, whatever the line break was; the line is counted        |
| `\xXX`                  | one byte from **exactly two** hex digits: `\x41` is `A`           |
| `\ddd`                  | one byte from **one to three** decimal digits, at most 255: `\65` is `A`, and `\0651` is `A1` because only three digits are read |
| `\z`                    | nothing: it skips the whitespace after it, line breaks included (and counts them), so a long string can be split across lines |
| `\u{XXX}`               | the UTF-8 bytes of a code point, any number of hex digits, below 2^31: `\u{E9}` is `é` (2 bytes) |
| anything else           | the error `invalid escape sequence`                               |

**UTF-8 encoding.** `append_utf8(out, code)` writes a code point the way Lua's own
`luaO_utf8esc` does, using the *original* UTF-8 scheme of up to 6 bytes. That covers every value
below 2^31, so surrogates such as `\u{D800}` and values above U+10FFFF are allowed, even though
they are not valid Unicode. The encoder fills continuation bytes (`10xxxxxx`, 6 bits each) from
the end, and each one leaves one bit less room in the first byte. The first byte then starts with
as many `1` bits as there are bytes in total. Code points below 128 are a single byte.

**Escape errors.** Each failing escape has Lua's exact message:

| Problem                              | Message                       | Example near text   |
| ------------------------------------ | ----------------------------- | ------------------- |
| unknown escape letter                | `invalid escape sequence`     | `'"abc\q'`          |
| `\x` without two hex digits          | `hexadecimal digit expected`  | `'"\x4"'`           |
| `\ddd` above 255                     | `decimal escape too large`    | `'"\256"'`          |
| `\u` without `{`                     | `missing '{'`                 | `'"\u4'`            |
| `\u{` without a hex digit            | `hexadecimal digit expected`  | `'"\u{}'`           |
| a code point of 2^31 or more         | `UTF-8 value too large`       | `'"\u{80000000'`    |
| no `}` after the digits              | `missing '}'`                 | `'"\u{48"'`         |

The near text is built by `escape_error(message, text, raw)`, the same way Lua builds it: the
string so far (`text`), the escape exactly as written so far (`raw`, such as `\x4`), and then
the character that broke it, unless the input ended. To stop `\u{}` from overflowing, the check
happens *before* each new digit: if the value is already above `0x7FFFFFF`, one more digit would
pass 2^31 - 1.

## Line counting

A line break is `\n`, `\r`, `\r\n` or `\n\r`, and each counts as **one** line. `read_newline()`
moves past the first character, and also past the next one if it is the *other* line-break
character. So `\r\n` is one line, but `\n\n` and `\r\r` are two.

## Not handled yet

- **Part 10:** long brackets. `[` is always `LBracket` (no `[[ ... ]]` strings), and `--[[`
  starts a short comment instead of a long one.
