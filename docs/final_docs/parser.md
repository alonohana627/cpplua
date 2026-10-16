# The parser

How the parser works today. This page describes the code as it is, and is updated as the parser
grows. The rules it implements are in [../spec/grammar.md](../spec/grammar.md), and the remaining
work is in [../plan/parser.md](../plan/parser.md).

**Status:** parts 0 and 1 of the plan. A chunk may be empty or hold one `return` with a list of
simple expressions (`nil`, `true`, `false`, numbers, strings, `...`). Anything else is an error
for now.

## Where it lives

| File                                  | Contents                                          |
| ------------------------------------- | ------------------------------------------------- |
| `lib/internal/parser/ast.hpp`         | the tree: ids, ranges, node structs, `Ast`        |
| `lib/parser/ast.cpp`                  | its implementation                                |
| `lib/internal/parser/parser.hpp`      | the `Parser` class                                |
| `lib/parser/parser.cpp`               | its implementation                                |
| `lib/internal/parser/ast_printer.hpp` | `print_ast()`, the tree as indented text          |
| `lib/parser/ast_printer.cpp`          | its implementation                                |
| `include/lua2/debug.hpp`              | `ast_dump()`, public, for `lua2 --ast`            |
| `lib/internal/overloaded.hpp`         | `Overloaded`, for `std::visit` with lambdas       |
| `lib/internal/text_format.hpp`        | `quoted()`, `format_float()`, shared by the debug views |
| `tests/parser_test.cpp`               | parser tests, tagged `[parser]` (and `[ast]`)     |

Like the lexer, the parser is internal to the library.

## What it does

```cpp
lua2::Parser parser{source, "test.lua"};  // the source and name must outlive the parser
std::expected<lua2::Ast, lua2::SyntaxError> ast = parser.parse_chunk();
```

`parse_chunk()` reads the whole chunk and returns its tree, or the first error (lexical or
syntax), never both and never by throwing.

From the command line, `lua2 --ast file.lua` prints the tree. For
`return nil, 0x10, 1e3, 'a\n', ...`:

```
Chunk
  Return
    Nil
    Integer 16
    Float 1000.0
    String "a\n"
    Vararg
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

### Nodes

Each kind of node is a small struct, and the node types are two `std::variant`s:

| Variant | Alternatives so far                                                           |
| ------- | ----------------------------------------------------------------------------- |
| `Expr`  | `NilExpr`, `TrueExpr`, `FalseExpr`, `IntegerExpr{value}`, `FloatExpr{value}`, `StringExpr{text}`, `VarargExpr` |
| `Stat`  | `ReturnStat{values}`, where `values` is an `IdRange<ExprId>`                    |

The `Ast` keeps one vector per variant (`expressions_`, `statements_`), plus:

- **Lines in parallel vectors.** Every node records the line it starts on, but not inside the
  node: `expression_lines_[i]` is the line of expression `i` (a *structure of arrays*). The
  variants stay small, and the interpreter can ignore lines until it needs one for an error.
- **One text buffer.** A string literal's bytes are copied into one shared `std::string`, and
  `StringExpr` holds a `TextRange{first, length}` into it: 8 bytes instead of a 32-byte
  `std::string` with its own allocation. `ast.text(range)` reads it back (the view is only valid
  until the next `add_text`).
- **Lists.** Lists of expressions work like statement lists: `add_expression_list(ids)` and
  `expressions(range)`.

Because nodes hold only numbers and ids, `Expr` and `Stat` are **trivially copyable**: copying a
node is a plain memory copy, and the vectors never run destructors. `static_assert`s in
`ast.hpp` keep it that way, so a node that tried to hold a `std::string` would not compile.

Code that walks the tree uses `std::visit` with one lambda per node type, through the small
`Overloaded` helper. With no catch-all lambda, forgetting a node type is a compile error.

## The parser

`Parser` is a **recursive-descent** parser, like Lua's `lparser.c`: one function per grammar rule.

| Function                  | Rule                                     | Notes |
| ------------------------- | ---------------------------------------- | ----- |
| `parse_chunk()`           | `chunk ::= block <eof>` (G2.1)           | after the block, anything left is `<eof> expected near 'x'` |
| `parse_block()`           | `block ::= { stat } [ retstat ]` (G2.2)  | for now only the optional `return`; other statements come in parts 5 to 8 |
| `parse_return()`          | `retstat ::= 'return' [ explist ] [ ';' ]` (G2.3) | values are skipped when the block ends or a `;` follows; one `;` is allowed |
| `parse_expression_list()` | `explist ::= exp { ',' exp }` (G4.6)     | collects ids, then stores them as one list |
| `parse_expression()`      | `exp` (G4.1)                             | for now only the simple expressions; anything else is `unexpected symbol` (G4.7) |
| `block_follow()`          | -                                        | whether the current token ends a block: `<eof>`, `end`, `else`, `elseif`, `until` |

**Why `return` must be last (G2.5).** `parse_block()` stops right after a `return`. Whatever
comes next must then be the end of the block, so `return 1 return 2` fails in `parse_chunk()`
with `<eof> expected near 'return'`, which is exactly Lua's message.

Each node records the line of its first token: `parse_return()` reads `current_.line` before it
moves past `return`.

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
lines to it. It visits each node with `std::visit`, so a node type it does not print is a compile
error.

Values are shown the way the debug views show them everywhere (`text_format.hpp`): strings in
double quotes with unprintable bytes escaped (`"a\tb\x00"`), and floats in their shortest exact
form with `.0` when they look like integers (`1000.0`).

`lua2 --ast` reaches the internal parser through `lua2::ast_dump(source, chunk_name)` in
`include/lua2/debug.hpp`, the same way `--tokens` uses `token_dump`.

## Not handled yet

- **Expressions** other than the simple ones: operators (part 2), names, parentheses, fields and
  calls (part 3), tables (part 4), functions (part 7). Until then they are `unexpected symbol`,
  even where Lua would accept them: `return x` fails.
- **Statements** other than `return` (parts 5 to 8). Until then they end the chunk early, and the
  token is reported as `<eof> expected near 'x'`.
- Scopes and name resolution (parts 9 and 10), the graph view (part 11).
