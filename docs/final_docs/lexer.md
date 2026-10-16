# The lexer

How the lexer works today. This page describes the code as it is, and is updated as the lexer
grows. The rules it implements are in [../spec/lexical.md](../spec/lexical.md), and the remaining
work is in [../plan/lexer.md](../plan/lexer.md).

**Status:** parts 0 to 3 of the plan: operators, whitespace, line counting, names and keywords.
Numbers, strings and comments are not lexed yet.

## Where it lives

| File                            | Contents                                         |
| ------------------------------- | ------------------------------------------------ |
| `lib/internal/lexer/token.hpp`  | `TokenKind`, `TokenValue`, `Token`, `keywords`   |
| `lib/internal/lexer/lexer.hpp`  | the `Lexer` class                                |
| `lib/lexer/lexer.cpp`           | its implementation                               |
| `tests/lexer_test.cpp`          | its tests, tagged `[lexer]`                      |

The lexer is internal to the library: embedders never see tokens.

## What it does

The lexer turns source text into tokens, one per call:

```cpp
lua2::Lexer lexer{"local x = y"};   // the text must outlive the lexer
lua2::Token token = lexer.next();   // Local, then Name("x"), Assign, Name("y"), EndOfStream
```

`next()` returns the next token. At the end of the input it returns `EndOfStream`, and keeps
returning it on every later call.

The lexer hands these tokens to the parser, which checks the grammar and builds the syntax tree.
The lexer itself never decides whether the code is valid Lua.

## Tokens

A `Token` has three fields:

- `kind`: a `TokenKind`, an `enum class` with one entry per kind of token.
- `line`: the line the token **starts** on, counted from 1.
- `value`: a `TokenValue`, a `std::variant` of `std::monostate`, `std::string`, `std::int64_t`
  and `double`. A `Name` holds its text as a `std::string`. Operators and keywords hold
  `std::monostate` (no value). Strings and numbers will use the other alternatives.

Tokens compare with `==` (kind, line and value), which keeps tests short.

`TokenKind` currently has:

- `EndOfStream`
- `Name`
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

The `Lexer` holds only three things:

- `source_`: a `std::string_view` of the text. The lexer does not copy it, so the caller keeps it
  alive. The constructor's `[[clang::lifetimebound]]` makes clang warn about obvious mistakes,
  such as passing a temporary `std::string`.
- `pos_`: the index of the next character to read.
- `line_`: the current line.

## How `next()` works

`next()` is a loop around a `switch` on the current character:

1. At the end of the input, return `EndOfStream`.
2. **Whitespace** (space, `\t`, `\v`, `\f`): skip it and loop again.
3. **Line breaks** (`\n`, `\r`): call `read_newline()` and loop again.
4. **Characters that are always one token** (`+ - * % ^ # & | ( ) { } [ ] ; ,`): return that
   token.
5. **Characters that may start a longer token** (`= ~ / : < > .`): look ahead with `peek()` and
   take the **longest** token that matches (*maximal munch*). So `==` is one token, `<<=` is `<<`
   then `=`, and `....` is `...` then `.`.
6. **A letter or `_`**: call `read_name()`, which returns a name or a keyword.
7. **Anything else** throws `std::logic_error("lexer: unsupported character ...")`. This is a
   placeholder until numbers, strings and stray characters are handled.

Private helpers:

| Helper              | What it does                                                     |
| ------------------- | ---------------------------------------------------------------- |
| `make(kind)`        | builds a token of `kind` on the current line, with no value      |
| `take(length, kind)`| builds the token, **then** moves past `length` characters        |
| `single(kind)`      | `take(1, kind)`                                                  |
| `peek(offset)`      | the character `offset` places ahead, or `'\0'` past the end      |
| `read_newline()`    | moves past one line break and counts the line                    |
| `read_name()`       | reads a name, then checks the keyword table                      |

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

## Line counting

A line break is `\n`, `\r`, `\r\n` or `\n\r`, and each counts as **one** line. `read_newline()`
moves past the first character, and also past the next one if it is the *other* line-break
character. So `\r\n` is one line, but `\n\n` and `\r\r` are two.

## Not handled yet

In plan order: error messages and stray characters, comments, numbers, strings, long strings and
long comments. Until then, `-` is always `Minus` (no `--` comments), `[` is always `LBracket` (no
`[[` strings), and a digit or a byte such as `@` throws the placeholder error.
