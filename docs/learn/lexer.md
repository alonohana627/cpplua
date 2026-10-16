# Lexers: what they are and what ours must do

Reading material for roadmap step 2. Nothing here is code we have written yet.

## 1. The job of a lexer

A Lua program arrives as a flat string of characters:

```lua
local x = 10 -- the answer
print(x * 2.5)
```

The parser does not want to reason about characters. It wants to work with *words*: "the keyword
`local`, then a name `x`, then `=`, then the integer 10, ...". Turning characters into those words
is the lexer's job (it is also called a *scanner* or *tokenizer*). Its output is a stream of
**tokens**:

```
Local  Name(x)  Assign  Integer(10)
Name(print)  LParen  Name(x)  Star  Float(2.5)  RParen  EndOfStream
```

Notice what is gone: spaces, newlines and the comment. The lexer throws away everything the grammar
does not care about, so the parser never sees it.

A compiler or interpreter usually works as a pipeline:

```
source text --lexer--> tokens --parser--> AST --interpreter/compiler--> result
```

Each stage has one concern. The lexer knows what a number or string looks like, but nothing
about what `if` means or where `end` must appear. The parser knows grammar, but nothing about escape
sequences. That separation is why lexers are easy to test in isolation: you feed in a string and
check the list of tokens you get back.

## 2. What a token carries

A token is small. It usually has:

- a **kind** (`Name`, `Integer`, `String`, `Plus`, `While`, `EndOfStream`, ...), usually an enum,
- a **value** for the kinds that have one (the name's text, the integer 10, the float 2.5, the
  string's contents *after* escapes are processed),
- a **position** (at least the line number) so errors can say `input:3: unexpected symbol near 'x'`.

The original text of a token is called its *lexeme*. For `"a\tb"` the lexeme has 6 characters
(quotes and backslash included), but the value is the 3-character string `a<TAB>b`.

## 3. How a lexer works

Almost every hand-written lexer is the same loop:

1. Skip whitespace and comments.
2. Look at the current character and decide what kind of token starts here.
3. Keep consuming characters while they still belong to that token.
4. Produce the token and repeat. At the end of input, produce `EndOfStream`.

Two rules decide the tricky cases:

- **Maximal munch**: always take the longest token possible. `==` is one token, not two `=`.
  `...` is one token, not `..` followed by `.`. In Lua this even means `3..2` is read as one
  (malformed) numeral rather than `3 .. 2`, so it is a lexer error.
- **Lookahead**: sometimes you must peek at the next character before deciding. A `-` could be
  minus or the start of a comment (`--`). A `[` could be an index bracket or a long string (`[[`
  or `[==[`). A `.` could be field access, concatenation `..`, varargs `...` or the start of a
  number `.5`.

Keywords are lexed like names, then checked against the keyword list: read `while` as a name and
then see that it is reserved. That is simpler than special-casing every keyword.

## 4. Lua 5.4's lexical rules (manual ֲ§3.1)

**Names**: letters, digits and `_`, not starting with a digit. Case-sensitive.

**The 22 keywords**: `and break do else elseif end false for function goto if in local nil not or
repeat return then true until while`

**Other tokens**:
`+ - * / % ^ #`, `& ~ | << >> //`, `== ~= <= >= < > =`, `( ) { } [ ] ::`, `; : , . .. ...`

**Short strings** use `'...'` or `"..."` and cannot contain a raw line break. Escapes:

- `\a \b \f \n \r \t \v \\ \" \'`
- a backslash followed by a real line break, which inserts a newline
- `\z`, which skips all following whitespace, line breaks included
- `\xXX`: exactly two hex digits
- `\ddd`: up to three decimal digits, value at most 255
- `\u{XXX}`: a code point below 2^31, written as UTF-8 (the braces are required)
- anything else after `\` is an error

**Long strings** look like `[[...]]` or, at level n, `[==[...]==]` with n `=` signs. They end at
the first closing bracket of the *same* level, process no escapes, and turn every line ending (CR,
LF, CRLF, LFCR) into `\n`. A line break right after the opening bracket is dropped.

**Comments**: `--` starts a comment that runs to the end of the line. If `--` is followed
by a long bracket (`--[[ ... ]]`, `--[==[ ... ]==]`), it is a long comment that runs to the
matching close, across lines.

**Numerals**:

- decimal: `3`, `3.0`, `3.1416`, `314.16e-2`, `.5`, `5.`
- hex: `0xff`, `0xA.8p1` (hex fraction, `p` = power of **two**, exponent written in decimal)
- integer or float? A numeral with a radix point or an exponent is a **float**. Otherwise it is an
  **integer** if it fits in 64 bits. A decimal integer that does not fit becomes a float.
  A hex integer that does not fit **wraps around** (`0xffffffffffffffff` is `-1`).
- the lexer does not produce negative numbers: `-5` is the token `-` followed by `5`. The parser
  handles unary minus.

**Line counting**: `\n`, `\r`, `\r\n` and `\n\r` each count as one line.

**Errors** Lua reports at this stage, worth matching later:
`unfinished string`, `unfinished long string`, `unfinished long comment`, `malformed number`,
`invalid escape sequence`, `decimal escape too large`, `UTF-8 value too large`.
They come out as `chunkname:line: message near 'text'`.

(Skipping a first line that starts with `#`, such as `#!/usr/bin/lua`, is done by the file loader,
not the lexer.)

## 5. Design questions for our lexer

Think about these before writing code. There are no wrong answers, only trade-offs we will choose
together:

1. **All at once or on demand?** Should `tokenize(source)` return a `std::vector<Token>`, or
   should the parser call `lexer.next()` each time it wants a token? (Lua's own lexer is on
   demand, with one token of lookahead.)
2. **Owning or borrowing text?** Can a `Name` token hold a `std::string_view` into the source, or
   must it own a `std::string`? What about a string literal whose value differs from its lexeme
   because of escapes?
3. **Errors**: throw an exception, or return `std::expected<Token, LexError>`?
4. **Numbers**: convert `"0x1p4"` to `double` yourself, or use `std::from_chars`? (Check whether
   `from_chars` handles hex floats and whether it rejects what Lua rejects.)

## 6. Reading list

Read in this order. The first two are enough to start.

1. **Crafting Interpreters, chapter 4 "Scanning"** by Robert Nystrom (free online at
   craftinginterpreters.com/scanning.html). The best gentle introduction. The language is Java and
   the target language is Lox, but the ideas map one to one.
2. **Lua 5.4 Reference Manual ֲ§3.1 "Lexical Conventions"** (lua.org/manual/5.4/manual.html#3.1).
   Short and precise: this is the spec our tests will follow.
3. **`llex.c` in the Lua 5.4 source** (github.com/lua/lua/blob/v5.4/llex.c, about 600 lines of C).
   The real thing. Read `llex()`, `read_numeral()`, `read_long_string()` and `read_string()`
   *after* you have tried writing your own, to compare.
4. Optional, deeper: *Compilers: Principles, Techniques, and Tools* (the "Dragon Book"), chapter 3,
   for the theory behind lexers (regular expressions, finite automata). It is not needed to write
   a hand-coded lexer like ours.

## 7. Check yourself

You are ready for step 2 when you can answer these:

- What tokens does `a.b..c...` produce?
- Why is `x = 3..2` an error but `x = 3 .. 2` fine?
- What string value does `[[` + a real line break + `hello]]` have? What about `"a\z      b"`?
- Is `1e2` an integer or a float? `0x10`? `9223372036854775808`?
- Where does the lexer's job end and the parser's begin for `-5`?

Answers: (1) `a` `.` `b` `..` `c` `...`; (2) maximal munch reads `3..2` as one numeral, which is
malformed; (3) `hello` (no leading newline) and `ab`; (4) float, integer 16, float (it is 2^63, one
past the largest integer); (5) the lexer emits `-` and `5`, and the parser builds "negate 5".
