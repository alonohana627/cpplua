# The lexer

How the lexer works today. This page describes the code as it is, and is updated as the lexer
grows. The rules it implements are in [../spec/lexical.md](../spec/lexical.md), and the remaining
work is in [../plan/lexer.md](../plan/lexer.md).

**Status:** parts 0 to 4 and 6 of the plan: operators, whitespace, line counting, names,
keywords, stray characters, the error type and decimal numbers. Comments (part 5), hex numbers,
strings and long brackets are not lexed yet.

## Where it lives

| File                            | Contents                                         |
| ------------------------------- | ------------------------------------------------ |
| `lib/internal/lexer/token.hpp`  | `TokenKind`, `TokenValue`, `Token`, `keywords`   |
| `lib/internal/lexer/lexer.hpp`  | the `Lexer` class                                |
| `lib/lexer/lexer.cpp`           | its implementation                               |
| `lib/internal/syntax_error.hpp` | `SyntaxError`, `near_eof`, `near_char`           |
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
  a `std::string`. An `Integer` holds a `std::int64_t` and a `Float` a `double`. Operators and
  keywords hold `std::monostate` (no value). Strings will hold a `std::string`.

Tokens compare with `==` (kind, line and value), which keeps tests short.

`TokenKind` currently has:

- `EndOfStream`
- `Name`
- `Other`, for a stray byte such as `@`
- `Integer` and `Float`, for numerals
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
4. **Characters that are always one token** (`+ - * % ^ # & | ( ) { } [ ] ; ,`): return that
   token.
5. **Characters that may start a longer token** (`= ~ / : < > .`): look ahead with `peek()` and
   take the **longest** token that matches (*maximal munch*). So `==` is one token, `<<=` is `<<`
   then `=`, and `....` is `...` then `.`. The one exception: a `.` followed by a digit starts a
   number, as in `.5`.
6. **A letter or `_`**: call `read_name()`, which returns a name or a keyword.
7. **A digit**: call `read_number()`, which returns an `Integer` or a `Float`.
8. **A quote** returns the temporary error `not supported yet near '''`, until strings are
   handled.
9. **Any other byte** (`@`, `$`, `!`, a byte above 127, even `\0`) becomes an `Other` token
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
| `read_number()`     | finds the end of a decimal numeral, then converts it             |
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

The errors so far are `malformed number` (an exponent without digits) and the temporary "not
supported yet" for quotes. Unfinished strings and bad escapes come with their parts.

## Numbers

`read_number()` handles decimal numerals in two steps.

**1. Find where the numeral ends.** The shape is

```
digits [ '.' digits ] [ ('e' | 'E') [ '+' | '-' ] digits ]
```

with at least one digit before or after the point: `3`, `3.0`, `.5`, `5.`, `314.16e-2`. The
lexer notes whether it saw a `.` or an exponent, because that decides the type. An exponent with
no digits after it (`1e`, `1e+`) is the error `malformed number near '1e+'`.

**2. Convert the text** with `std::from_chars`, which ignores the locale, never allocates, and
reports problems through an error code rather than an exception:

- **No point and no exponent** means an integer. If it fits in `std::int64_t` the token is an
  `Integer`. If it does not (`9223372036854775808` and up), Lua reads it as a float instead, so
  conversion falls through to the next step.
- **Otherwise** the token is a `Float`. A value too large is infinity (`1e400`) and a value too
  small is 0 (`1e-400`). That is not an error in Lua. `from_chars` reports those cases as
  `errc::result_out_of_range`, but libc++ still stores infinity or 0, so that code is accepted.
  Tests pin this down in case a library ever behaves differently.

Numerals have no sign: `-5` is `Minus` then `Integer 5`. The parser turns that into a negation.
This is why `-9223372036854775808` is minus a *float*: the integer part alone is too large.

## Line counting

A line break is `\n`, `\r`, `\r\n` or `\n\r`, and each counts as **one** line. `read_newline()`
moves past the first character, and also past the next one if it is the *other* line-break
character. So `\r\n` is one line, but `\n\n` and `\r\r` are two.

## Not handled yet

In plan order: comments (part 5), hex numbers and malformed numbers (part 7), strings, long
strings and long comments. Until then:

- `-` is always `Minus` (no `--` comments) and `[` is always `LBracket` (no `[[` strings).
- A quote returns the temporary "not supported yet" error.
- Numbers stop where a decimal numeral stops, so `0x10` is `Integer 0` then `Name x10`, and `3x`
  or `1.2.3` are split into several tokens. In Lua those are hex or malformed numbers. Part 7
  reads numerals greedily, like Lua, and fixes this.
