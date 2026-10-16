# The parser

How the parser works today. This page describes the code as it is, and is updated as the parser
grows. The rules it implements are in [../spec/grammar.md](../spec/grammar.md), and the remaining
work is in [../plan/parser.md](../plan/parser.md).

**Status:** part 0 of the plan, the foundations. The parser accepts an empty chunk (only
whitespace and comments) and reports anything else as `<eof> expected`. No statements or
expressions exist yet.

## Where it lives

| File                                  | Contents                                          |
| ------------------------------------- | ------------------------------------------------- |
| `lib/internal/parser/ast.hpp`         | the tree: `NodeId`, `IdRange`, `Block`, `Ast`     |
| `lib/parser/ast.cpp`                  | its implementation                                |
| `lib/internal/parser/parser.hpp`      | the `Parser` class                                |
| `lib/parser/parser.cpp`               | its implementation                                |
| `lib/internal/parser/ast_printer.hpp` | `print_ast()`, the tree as indented text          |
| `lib/parser/ast_printer.cpp`          | its implementation                                |
| `include/lua2/debug.hpp`              | `ast_dump()`, public, for `lua2 --ast`            |
| `tests/parser_test.cpp`               | parser tests, tagged `[parser]` (and `[ast]`)     |

Like the lexer, the parser is internal to the library.

## What it does

```cpp
lua2::Parser parser{source, "test.lua"};  // the source and name must outlive the parser
std::expected<lua2::Ast, lua2::SyntaxError> ast = parser.parse_chunk();
```

`parse_chunk()` reads the whole chunk and returns its tree, or the first error (lexical or
syntax), never both and never by throwing.

From the command line, `lua2 --ast file.lua` prints the tree:

```
Chunk
```

## The tree: an arena

The tree is an **arena** (a project decision; see [../learn/ast_representation.md](../learn/ast_representation.md)
for the comparison with the alternatives). All nodes live in `std::vector`s owned by one `Ast`,
and nodes refer to each other by index rather than by pointer.

- **`NodeId<Tag>`** is a 32-bit index. `ExprId = NodeId<ExprTag>` and
  `StatId = NodeId<StatTag>` are different types, so passing a statement where an expression is
  expected does not compile. An id is 4 bytes, half a pointer.
- **`IdRange<Id>`** is a `(first, count)` run of ids inside one of the `Ast`'s list vectors. A
  `Block` holds an `IdRange<StatId>` for its statements instead of its own `std::vector`, so the
  statement lists of every block share one allocation.
- **`Ast::add_statement_list(ids)`** appends a finished list and returns its range;
  **`Ast::statements(block)`** gives it back as a `std::span`. While parsing, a block collects its
  statements in a temporary vector first, because nested blocks add their own lists in between,
  then stores them in one go, so each block's list stays contiguous.

So far the `Ast` holds only the root `Block` and the statement-list vector. Each part of the plan
adds its node structs (each kind of node a `std::variant` alternative) and their vectors.

## The parser

`Parser` is a **recursive-descent** parser, like Lua's `lparser.c`: one function per grammar rule.
For now there are two: `parse_chunk()` (G2.1) and `parse_block()` (G2.2), which returns an empty
block until statements exist.

### Moving through tokens

The parser holds:

- `current_`: the token being looked at;
- `lookahead_`: an optional second token, filled only by `peek()`;
- the `Lexer`, the source (for error text), and the `Ast` being built.

This is the **one-token buffer** chosen for lookahead (a project decision; measured faster than
lexing everything into a vector first). The helpers every grammar rule will use:

| Helper          | What it does                                                            |
| --------------- | ----------------------------------------------------------------------- |
| `advance()`     | moves to the next token: the lookahead one if `peek()` read it, else a new one from the lexer |
| `peek()`        | the token after `current_`, without moving (only table fields need it, G6.5) |
| `check(kind)`   | whether `current_` is of `kind`                                         |
| `accept(kind)`  | if `current_` is of `kind`, moves past it and returns `true`            |
| `expect(kind)`  | `current_` must be of `kind`: moves past it, or returns `'X' expected`  |

Every helper that reads a token can fail (a lexer error), so they return `std::expected`, and a
lexer error reaches the caller unchanged: `unfinished string near <eof>` from the parser is the
lexer's own message.

### Errors

Parser errors use the lexer's `SyntaxError`, with the line of the **current** token:

```
input:1: <eof> expected near 'x'
```

- `error(message)` builds one at the current token, and records the C++ `origin` like the
  lexer does.
- `error_expected(kind)` builds `'X' expected`. The name of the expected token comes from
  `expected_text(kind)` in `lib/lexer/token.cpp`, which follows Lua: keywords and symbols in
  quotes (`'end'`, `'=='`), and kinds without a fixed spelling in angle brackets and **not**
  quoted (`<eof>`, `<name>`).
- `near_current()` shows the current token the way Lua does (G9.2):
  - `<eof>` at the end of the input;
  - otherwise the token **as written**, in quotes: `'0x10'`, not `'16'`. This is why every
    `Token` now carries its `offset` and `length` in the source (see the lexer page);
  - a stray byte through `near_char()`, so an unprintable one shows as `'<\128>'`;
  - a short string is the one exception: like Lua, it shows its value *after* escapes, between
    its own quotes, so `"\65"` shows as `'"A"'`. A long string shows as written, brackets
    included.

## The AST printer

`print_ast(ast)` turns a tree into indented text, one node per line, two spaces per level. It is
what `lua2 --ast` prints, and what parser tests compare against, so each part adds its nodes'
lines to it. Today every tree prints as just `Chunk`.

`lua2 --ast` reaches the internal parser through `lua2::ast_dump(source, chunk_name)` in
`include/lua2/debug.hpp`, the same way `--tokens` uses `token_dump`.

## Not handled yet

Everything in the grammar: statements, expressions, functions and tables arrive in parts 1 to 8,
scopes and name resolution in parts 9 and 10, the graph view in part 11.
