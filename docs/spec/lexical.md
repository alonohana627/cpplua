# Lua 5.4 lexical rules

The complete specification our lexer implements. It is based on the Lua 5.4 Reference Manual ֲ§3.1.
Where the manual is silent, the behavior of the reference implementation (`llex.c`, `lobject.c`
in Lua 5.4) is the tie-breaker, and those rules are marked **(impl)**.

Every rule has an id like `L5.3`. Tests should name the rule they check, for example
`TEST_CASE("L5.3 hex integers wrap around", "[lexer]")`.

---

## L1. Input

- **L1.1** The input (a *chunk*) is a sequence of bytes. It is not required to be valid UTF-8.
- **L1.2** The lexer reads bytes left to right and produces tokens until end of input, then
  produces one `EndOfStream` token.
- **L1.3** Character classes are ASCII and independent of locale **(impl)**:
  - *letter*: `A`-`Z`, `a`-`z`, `_`
  - *digit*: `0`-`9`
  - *hex digit*: `0`-`9`, `A`-`F`, `a`-`f`
  - *whitespace*: space, `\t` (9), `\v` (11), `\f` (12), `\n` (10), `\r` (13)
  - Bytes 128-255 are not letters.
- **L1.4** Skipping a first line that starts with `#` (a shebang) is **not** the lexer's job. The
  file loader does it before the lexer runs, and keeps line numbers correct.

## L2. Lines

- **L2.1** A *line break* is one of `\n`, `\r`, `\r\n`, `\n\r`. Each counts as one line.
  `\n\n` and `\r\r` are two line breaks. **(impl)**
- **L2.2** Lines are numbered from 1. Every token records the line it starts on.
- **L2.3** Error messages use the line where the lexer currently is, which for an unfinished
  multi-line token is its last line. **(impl)**

## L3. Whitespace and comments

- **L3.1** Whitespace (L1.3) separates tokens and is otherwise ignored.
- **L3.2** `--` starts a comment anywhere outside a string.
- **L3.3** If the text right after `--` is an opening long bracket (L6.1), the comment is a
  **long comment**. It runs up to the matching closing long bracket and may span lines.
- **L3.4** Otherwise it is a **short comment** and runs to the end of the line. The line break is
  not part of the comment. This includes `--[`, `--[=`, `--[==x` and similar text that does not
  form a valid opening long bracket. **(impl)**
- **L3.5** A long comment that reaches end of input is an error: `unfinished long comment
  (starting at line N)`, near `<eof>`. **(impl)**
- **L3.6** A short comment may end at end of input.

## L4. Names and keywords

- **L4.1** A *name* is a letter followed by any number of letters and digits (L1.3).
- **L4.2** Names are case-sensitive: `And` and `AND` are names, `and` is a keyword.
- **L4.3** These 22 words are **reserved** and are never names:

  ```
  and       break     do        else      elseif    end
  false     for       function  goto      if        in
  local     nil       not       or        repeat    return
  then      true      until     while
  ```

- **L4.4** Keywords follow maximal munch like names: `whilex` is the name `whilex`, and `while_`
  is a name.
- **L4.5** By convention, names starting with `_` followed by capital letters (such as `_ENV`,
  `_G`, `_VERSION`) are used by Lua itself. They are ordinary names to the lexer.

## L5. Numerals

### Shape

- **L5.1** A numeral starts with a digit, or with `.` followed by a digit.
- **L5.2** The lexer reads a numeral greedily **(impl)**:
  1. Read the first character. If the numeral starts with `0x` or `0X`, it is **hexadecimal**:
     read that prefix too, and its exponent letters are `p`/`P`. Otherwise its exponent letters
     are `e`/`E`.
  2. Repeat: if the next character is an exponent letter, read it and then an optional `+` or `-`.
     Otherwise, if it is a hex digit or `.`, read it. Otherwise stop.
  3. If the next character is a letter (L1.3), read it too. This forces the error in L5.3 for
     things like `3x` or `0xg`.
  4. Convert the text read so far (L5.4-L5.8). If that fails, the error is `malformed number`
     near the text read.

  Note that step 2 reads hex digits even in a decimal numeral, so `3a`, `1.2.3` and `3..2` are
  all read as one numeral and are malformed.
- **L5.3** Examples of malformed numbers: `3x`, `3..2`, `1.2.3`, `1e`, `1e+`, `0x`, `0x.p1`,
  `0xg`, `08z`.

### Value

- **L5.4** A **decimal** numeral is: digits with an optional fractional part (`.` and more
  digits), with at least one digit overall, then an optional exponent (`e`/`E`, optional sign, one or
  more decimal digits). Valid: `3`, `345`, `3.0`, `3.1416`, `314.16e-2`, `0.31416E1`, `34e1`, `.5`,
  `5.`, `007`. Leading zeros do not mean octal.
- **L5.5** A **hexadecimal** numeral is: `0x` or `0X`, hex digits with an optional fractional
  part, with at least one hex digit overall, then an optional binary exponent (`p`/`P`, optional
  sign, one or more **decimal** digits; the value is multiplied by 2 to that power).
  Valid: `0xff`, `0xBEBADA`, `0x0.1E`, `0xA23p-4`, `0X1.921FB54442D18P+1`, `0x.8`, `0x8.`.
  In hex, `e` is a digit: `0x1e` is the integer 30, while `1e5` is a decimal float.
- **L5.6** A numeral with a `.` or an exponent is a **float** (64-bit IEEE double). Otherwise it is
  an integer candidate.
- **L5.7** A decimal integer candidate is an **integer** (64-bit signed) if it is at most
  `9223372036854775807` (2^63 - 1). Otherwise it becomes the float with that value, rounded:
  `9223372036854775808` is the float 2^63.
- **L5.8** A hexadecimal integer candidate is always an **integer**. It is taken modulo 2^64 and
  then read as signed: `0xffffffffffffffff` is `-1`, `0x7fffffffffffffff` is the largest integer,
  and `0x10000000000000000` is `0`.
- **L5.9** Floats round to the nearest double. Overflow gives infinity (`1e400`), and underflow gives
  0 (`1e-400`). The words `inf` and `nan` are never numerals. **(impl)**
- **L5.10** Numerals have no sign. `-5` is the token `-` followed by the numeral `5`. So
  `-9223372036854775808` in source is a negated **float**, not the smallest integer.

## L6. Long brackets

- **L6.1** An **opening long bracket of level n** is `[`, then n `=` signs, then `[`
  (n >= 0). A **closing long bracket of level n** is `]`, n `=`, `]`.
  `[[` is level 0, `[==[` is level 2.
- **L6.2** `[` followed by one or more `=` and then anything other than `[` is an error:
  `invalid long string delimiter`. **(impl)**
- **L6.3** `[` not followed by `=` or `[` is the ordinary `[` token.
- **L6.4** A `]` not followed by `=*]` is ordinary text inside a long bracket, and ordinary `]`
  token outside one.

## L7. Strings

### Short strings

- **L7.1** A short string is delimited by matching `'` or `"`. The other quote may appear inside
  unescaped: `"it's"`, `'say "hi"'`.
- **L7.2** A raw line break inside a short string is an error: `unfinished string`, near the
  string read so far. End of input inside one is also `unfinished string`, near `<eof>`. **(impl)**
- **L7.3** A backslash starts an escape sequence:

  | Escape        | Value                                                                  |
  | ------------- | ---------------------------------------------------------------------- |
  | `\a`          | bell (7)                                                               |
  | `\b`          | backspace (8)                                                          |
  | `\f`          | form feed (12)                                                         |
  | `\n`          | newline (10)                                                           |
  | `\r`          | carriage return (13)                                                   |
  | `\t`          | horizontal tab (9)                                                     |
  | `\v`          | vertical tab (11)                                                      |
  | `\\`          | backslash                                                              |
  | `\"`          | double quote                                                           |
  | `\'`          | single quote                                                           |
  | `\` + line break | a single `\n`, whatever the line break was (L2.1); the line count goes up |
  | `\z`          | skips all following whitespace (L1.3), line breaks included, and counts them |
  | `\xXX`        | the byte with that value; **exactly** two hex digits                   |
  | `\ddd`        | the byte with that value; **one to three** decimal digits, at most 255 |
  | `\u{XXX}`     | the UTF-8 encoding of code point XXX (one or more hex digits)          |

- **L7.4** `\ddd` reads at most three digits, so `"\0651"` is `A1` and `"\65"` is `A`. Bytes
  above 127 and zero bytes are allowed: `"\0"` is a one-byte string.
- **L7.5** `\u{XXX}` accepts code points from 0 up to 2^31 - 1 (`7FFFFFFF`), with any number of
  leading zeros. Values above `10FFFF` and surrogates are allowed and encoded with the original
  UTF-8 scheme of up to 6 bytes. **(impl)**
- **L7.6** Escape errors, each reported near the string read so far **(impl)**:
  - any other character after `\`: `invalid escape sequence`
  - `\x` without two hex digits: `hexadecimal digit expected`
  - `\ddd` above 255: `decimal escape too large`
  - `\u` not followed by `{`: `missing '{'`
  - `\u{` not followed by a hex digit: `hexadecimal digit expected`
  - a code point of 2^31 or more: `UTF-8 value too large`
  - no `}` after the digits: `missing '}'`
- **L7.7** Any byte other than `\`, the closing quote and a line break stands for itself.

### Long strings

- **L7.8** A long string starts with an opening long bracket of level n and ends at the first
  closing long bracket of the **same** level n. Brackets of other levels inside it are text:
  `[==[ a ]] b ]=] c ]==]` is ` a ]] b ]=] c `.
- **L7.9** Nothing is interpreted inside: no escapes, so `[[\n]]` is two characters.
- **L7.10** Every line break (L2.1) inside becomes a single `\n`.
- **L7.11** If a line break comes **right after** the opening bracket, it is dropped. Only that one
  line break is dropped.
- **L7.12** End of input inside a long string is an error: `unfinished long string (starting at
  line N)`, near `<eof>`.

## L8. Operators and punctuation

- **L8.1** The other tokens are:

  ```
  +     -     *     /     %     ^     #
  &     ~     |     <<    >>    //
  ==    ~=    <=    >=    <     >     =
  (     )     {     }     [     ]     ::
  ;     :     ,     .     ..    ...
  ```

- **L8.2** Maximal munch: the longest token wins. `<=` not `<` `=`, `//` not `/` `/`, `...` not
  `..` `.`. Examples: `a<<=b` is `a` `<<` `=` `b`; `a....b` is `a` `...` `.` `b`; `x//y` is
  `x` `//` `y`.
- **L8.3** `-` immediately followed by `-` is a comment (L3), never two minus signs.
- **L8.4** `.` followed by a digit starts a numeral (L5.1). `..` followed by a digit is the `..`
  token then a numeral: `a..5` is `a` `..` `5`.
- **L8.5** `[` is decided by L6.2/L6.3.
- **L8.6** `~` alone is a token (binary xor and unary bitwise not). `!=` is not a Lua token: it is
  `!` followed by `=`.
- **L8.7** `<const>` and `<close>` are not tokens: they are `<`, a name and `>`, which the parser
  recognizes.

## L9. Other characters

- **L9.1** Any other byte (for example `@`, `$`, `!`, `` ` ``, `?`, or a byte above 127) is
  not a lexer error. It becomes a single-character token by itself, and the parser rejects it with
  `unexpected symbol near '@'`. **(impl)**

## L10. Error messages

- **L10.1** A lexical error stops lexing. It is reported as

  ```
  <chunkname>:<line>: <message> near <token>
  ```

  For example `input:1: malformed number near '3x'`.
- **L10.2** The `<token>` part is **(impl)**:
  - for names, strings and numerals: the text read so far, in single quotes. For a string that
    includes the opening quote: `near '"abc'`.
  - for end of input: `<eof>`.
  - for a printable single character: that character in quotes, `'@'`.
  - for a non-printable byte: its decimal value, `'<\200>'`.
  - for keywords and multi-character symbols: the text in quotes, `'while'`, `'...'`.
- **L10.3** Chunk names shown in messages: a file `test.lua` appears as `test.lua`, standard input as
  `stdin`, and a string passed to `load` as `[string "first line..."]`. This is formatted outside
  the lexer, but the lexer receives the chunk name to put in messages.

- **L10.4** The reference implementation also has size limits (`chunk has too many lines`,
  `lexical element too long`). They are far beyond any real program, and we do not need to
  match them exactly.

## L11. Token kinds our lexer produces

A summary for designing the `Token` type. It is not a rule:

- `EndOfStream`
- `Name` (with its text), `String` (with its processed bytes), `Integer` (`std::int64_t`),
  `Float` (`double`)
- one kind per keyword (L4.3): 22 kinds
- one kind per operator or punctuation (L8.1): 33 kinds
- `Other` with the single byte (L9.1)
