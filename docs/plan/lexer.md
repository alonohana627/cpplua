# Lexer plan

Roadmap step 2, cut into small parts. Each part adds one kind of token, has its own tests, and ends
with everything green (`build`, `ctest`, `tidy`, `format-check`). Rule ids such as `L5.8` point to
[../spec/lexical.md](../spec/lexical.md). You only need the rules listed for the current part.

Finishing part 11 makes the lexer Lua 5.4 compatible and bumps the version to 0.0.2.

## Defaults (change them if you disagree)

- **On demand**: `Lexer::next()` returns one token at a time, like Lua's own lexer. The parser will
  pull tokens as it needs them.
- **Tokens own their values**: `std::string` for names and strings, `std::int64_t` and `double` for
  numbers, held in a `std::variant`. This is simple first, and we can optimize later.
- **Errors throw** a `SyntaxError` exception (Lua's own lexer stops at the first error too).
- **Files**: the lexer is internal: sources in `lib/lexer/`, headers in `lib/internal/lexer/`
  (not `include/`). Tests may include headers from `lib/internal/` so internals can be tested
  directly.

## Parts

### Part 0: Token and an empty lexer

- `TokenKind` enum with just `EndOfStream` for now, a `Token` struct (`kind`, `line`, value),
  and `Lexer(std::string_view source)` with `Token next()`.
- **Tests**: empty input gives `EndOfStream` on line 1, and calling `next()` again still gives
  `EndOfStream`.
- **You learn**: the shape everything else plugs into.

### Part 1: Single-character tokens, whitespace and lines

- Rules: L1.3, L2.1, L2.2, L3.1, and the one-character entries of L8.1 (`+ - * / % ^ # & ~ | < > =
  ( ) { } [ ] ; : , .`).
- **Tests**: `"( )"` gives `(` `)` EOS; tokens on lines 1, 2, 3 for each of `\n`, `\r`,
  `\r\n`, `\n\r`; `\n\n` counts two lines.
- **You learn**: the main loop: skip, look at one character, decide.

### Part 2: Multi-character operators

- Rules: L8.1 (the rest), L8.2, L8.4 (just `..` and `...` for now), L8.6.
- **Tests**: every operator alone; the maximal munch examples in L8.2; `a.b`, `a..b`, `a...`.
- **You learn**: one character of lookahead (`peek`) and maximal munch.

### Part 3: Names and keywords

- Rules: L4.1-L4.5.
- **Tests**: `x`, `_x1`, `_ENV`; all 22 keywords; `whilex`, `While`, `end_`; names touching
  operators (`a+b`).
- **You learn**: lex keywords as names, then look them up (a `constexpr` table or a switch).

### Part 4: Errors and stray characters

- Rules: L9.1, L10.1, L10.2 (the single-character and `<eof>` cases).
- Add the chunk name to the lexer and a `SyntaxError` with the full `chunk:line: message near
  'x'` text. Nothing produces a lexer error yet, so test the helper that builds the message.
- `@`, `$`, `!` and bytes above 127 become an `Other` token, which is **not** an error.
- **You learn**: how Lua formats errors, before the parts that need them.

### Part 5: Short comments

- Rules: L3.2, L3.4, L3.6, L8.3.
- **Tests**: `a -- c` gives `a` EOS; comment at end of input; `a - b` stays three tokens;
  `--[` and `--[=x` are short comments; line numbers after a comment.
- (Long comments wait for part 9, where long brackets arrive.)

### Part 6: Decimal numbers

- Rules: L5.1, L5.4, L5.6, L5.7, L5.9, L5.10.
- **Tests**: `3`, `007`, `3.0`, `.5`, `5.`, `1e10`, `314.16e-2`; integer vs float kind;
  `9223372036854775807` is an integer and `9223372036854775808` is a float; `1e400` is infinity;
  `-5` is two tokens.
- **You learn**: `std::from_chars` for `int64_t` and `double` and its error codes. (Checked on our
  libc++: `1e400` gives `inf` but reports `errc::result_out_of_range`, which Lua treats as success.)

### Part 7: Hex numbers and malformed numbers

- Rules: L5.2, L5.3, L5.5, L5.8.
- **Tests**: `0xff`, `0X1e` (30), `0x.8`, `0xA23p-4`, `0x1p4` (16.0); wrapping
  (`0xffffffffffffffff` is -1); every malformed example in L5.3 throws `malformed number near '...'`.
- **You learn**: reading greedily first and validating second, like `llex.c`. Hex floats need
  their own conversion (`std::from_chars` with `chars_format::hex` or a hand-written loop).

### Part 8: Short strings, simple escapes

- Rules: L7.1, L7.2, L7.7, and the named escapes in L7.3 (`\a \b \f \n \r \t \v \\ \" \'`, backslash + line
  break).
- **Tests**: `"abc"`, `'it"s'`, empty string; each escape; `unfinished string` at a line break and
  at end of input; `invalid escape sequence` for `\q`.

### Part 9: Numeric escapes, `\u{}` and `\z`

- Rules: the rest of L7.3, L7.4-L7.6.
- **Tests**: `\65`, `\0651`, `\0`, `\255`, `\256` (error), `\x41`, `\x4` (error), `\u{48}`,
  `\u{7FFFFFFF}` (6 bytes), `\u{80000000}` (error), missing braces; `"a\z    b"` and `\z` across
  lines (line count still right).
- **You learn**: hand-writing a UTF-8 encoder.

### Part 10: Long brackets: long strings and long comments

- Rules: L6.1-L6.4, L7.8-L7.12, L3.3, L3.5.
- **Tests**: `[[x]]`, `[==[ a ]] b ]=] c ]==]`, first line break dropped (each line break form), line
  breaks normalized, no escapes (`[[\n]]`), `[=x` is `invalid long string delimiter`, `[` alone
  is the bracket token, unfinished long string/comment with the starting line, `--[[ ... ]]` and
  `--[==[ ... ]==]` spanning lines.
- **You learn**: one helper (count the `=` signs) shared by strings and comments.

### Part 11: Conformance sweep

- A debug flag `lua2 --tokens file.lua` that prints one token per line.
- Lex every `.lua` file in the official Lua 5.4 test suite (`lua-5.4.x-tests`) and check there
  are no errors and the line numbers look right. Add any surprise as a new test.
- Bump the version to 0.0.2. The lexer is done.

## How we work through a part

1. You read the rules listed for the part.
2. I write the failing tests for it (or you do, if you prefer).
3. You write the code until they pass. Ask whenever you get stuck.
4. I review: correctness against the spec, C++26 idioms, and anything tidy did not catch.
