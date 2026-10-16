# The parser

How the parser works today. This page describes the code as it is, and is updated as the parser
grows. The rules it implements are in [../spec/grammar.md](../spec/grammar.md), and the remaining
work is in [../plan/parser.md](../plan/parser.md).

**Status:** parts 0 to 4 of the plan. A chunk may be empty or hold one `return` with a list of
expressions: simple values (`nil`, `true`, `false`, numbers, strings, `...`), names,
parentheses, fields, indexing, calls, method calls and table constructors, combined with every
unary and binary operator at Lua's precedence. Functions and other statements come later.

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
| `Expr`  | `NilExpr`, `TrueExpr`, `FalseExpr`, `IntegerExpr{value}`, `FloatExpr{value}`, `StringExpr{text}`, `VarargExpr`, `UnaryExpr{op, operand}`, `BinaryExpr{op, left, right}`, `NameExpr{name}`, `ParenExpr{inner}`, `IndexExpr{object, key}`, `CallExpr{function, arguments}`, `MethodCallExpr{object, method, arguments}`, `TableExpr{fields}` |
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
  `expressions(range)`. Table fields have a list vector of their own, holding the small
  `TableField` structs directly: `add_field_list(fields)` and `fields(range)`, with
  `IdRange<FieldId>` ranges.

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
| `parse_expression()`      | `exp` (G4.1)                             | `parse_subexpression(0)`                  |
| `parse_subexpression(limit)` | `exp binop exp`, `unop exp` (G7)      | precedence climbing, see below            |
| `parse_simple_expression()` | the simple values of G4.1             | anything else goes to `parse_suffixed_expression()` |
| `parse_suffixed_expression()` | a prefixexp with its suffixes (G4.8) | Lua's `suffixedexp()`, see "Names, fields and calls" |
| `parse_primary_expression()` | `Name` or `( exp )` (G4.2)            | anything else is `unexpected symbol` (G4.7) |
| `parse_call_arguments()`  | `args` (G4.5)                            | `( explist )`, or one table or string |
| `parse_table()`           | `tableconstructor` (G6.1, G6.2)          | Lua's `constructor()`, see "Tables" |
| `parse_table_field()`     | `field` (G6.3)                           | the one user of `peek()`                  |
| `expect_name()`           | -                                        | a `Name`, copied into the text buffer, or `<name> expected` |
| `expect_match(closing, opening, line)` | -                           | like `expect`, but names the opening token when it is on another line |
| `block_follow()`          | -                                        | whether the current token ends a block: `<eof>`, `end`, `else`, `elseif`, `until` |

**Why `return` must be last (G2.5).** `parse_block()` stops right after a `return`. Whatever
comes next must then be the end of the block, so `return 1 return 2` fails in `parse_chunk()`
with `<eof> expected near 'return'`, which is exactly Lua's message.

Each node records the line of its first token: `parse_return()` reads `current_.line` before it
moves past `return`. Binary nodes are the exception: they record the line of their **operator**,
as Lua does, because that is the line a run-time error like "attempt to add a nil value" will
point at.

## Operators and precedence

`UnaryExpr` holds a `UnaryOp` (`Negate`, `Not`, `Length`, `BitwiseNot`) and its operand.
`BinaryExpr` holds a `BinaryOp` (21 of them: `Add`, `Concat`, `And`, `ShiftLeft`, ...) and its
two sides. `operator_text(op)` gives the spelling (`"+"`, `"not"`, `".."`), used by the printer.

`parse_subexpression(limit)` is **precedence climbing**, a direct port of Lua's `subexpr()`.
Each binary operator has two priorities, from Lua's own table (G7.8):

| Operator               | Left | Right |
| ---------------------- | ---- | ----- |
| `or`                   | 1    | 1     |
| `and`                  | 2    | 2     |
| `< > <= >= ~= ==`      | 3    | 3     |
| `\|`                   | 4    | 4     |
| `~`                    | 5    | 5     |
| `&`                    | 6    | 6     |
| `<< >>`                | 7    | 7     |
| `..`                   | 9    | 8     |
| `+ -`                  | 10   | 10    |
| `* / // %`             | 11   | 11    |
| unary `- not # ~`      | 12   |       |
| `^`                    | 14   | 13    |

The function works in two steps:

1. **Read one operand.** If the current token is a unary operator, take it and parse its operand
   with limit 12. Otherwise read a simple expression.
2. **Extend it.** While the next token is a binary operator whose *left* priority is **greater**
   than `limit`, take it, parse its right side with the operator's *right* priority as the new
   limit, and make the result the new left side.

Why this gives Lua's rules:

- **Precedence:** in `1 + 2 * 3`, the right side of `+` is parsed with limit 10. `*` (11) is
  greater, so it is taken inside: `1 + (2 * 3)`. In `1 * 2 + 3`, the right side of `*` has limit
  11, `+` (10) is not greater, so it stops, and the outer loop takes `+`: `(1 * 2) + 3`.
- **Left associativity:** in `1 - 2 - 3`, the right side of the first `-` has limit 10, and the
  second `-` (10) is not *greater*, so it is left for the outer loop: `(1 - 2) - 3`.
- **Right associativity:** `..` has right priority 8, lower than its left priority 9. In
  `a .. b .. c`, the right side of the first `..` has limit 8, and the second `..` (9) is
  greater, so it is taken inside: `a .. (b .. c)`. The same for `^` (14/13).
- **Unary operators and `^`:** a unary operand is parsed with limit 12. `^` (14) is greater, so
  `-2 ^ 2` is `-(2 ^ 2)`. `*` (11) is not, so `-2 * 3` is `(-2) * 3`. On the right of `^`, the
  limit is 13, but a unary operator is read in step 1 regardless, so `2 ^ -3` works.

The tables are `constexpr` functions over the `TokenKind`: `unary_operator()`,
`binary_operator()` and `priority()`. `-` and `~` appear in both, as unary and as binary:
step 1 only looks for unary operators and step 2 only for binary ones, so the position decides.

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

## Names, fields and calls

These are the **suffixed expressions**: a start, then any number of suffixes applied left to
right. `a.b[c](d):e"f"` is one expression: `a`, then `.b`, then `[c]`, then `(d)`, then `:e"f"`.

`parse_primary_expression()` reads the start, and only two tokens can begin one (G4.2):

- a `Name`, giving a `NameExpr`;
- `(`, giving a `ParenExpr` around an expression, closed with `expect_match`.

Anything else is `unexpected symbol`. So a literal cannot be called or indexed directly: in
`return 's':upper()`, the string is a complete simple expression, and the `:` left over gives
`<eof> expected near ':'`, exactly as in Lua. `('s'):upper()` works.

`parse_suffixed_expression()` then loops over suffixes, each one wrapping the expression so far:

| Suffix          | Node                                            |
| --------------- | ----------------------------------------------- |
| `.name`         | `IndexExpr` with a `StringExpr` key: `a.b` is `a["b"]` |
| `[exp]`         | `IndexExpr`                                     |
| `:name args`    | `MethodCallExpr`; the arguments are required (`function arguments expected`) |
| `(args)`, a table or a string | `CallExpr`                        |

A string right after an expression is a call with that one argument (G4.10): `require 'mod'` is
`require('mod')`, and `f'a''b'` is `f('a')('b')`.

Because suffixes are part of the *operand*, they bind tighter than every operator: `-f(x)` is
`-(f(x))`, and `#t.list` is `#(t.list)`.

**Parentheses are a real node.** `ParenExpr` is kept even though the tree shape already shows
grouping, because in Lua parentheses change the meaning: `(f())` is only the first value of the
call, and `(a)` is not a variable, so it cannot be assigned to (G4.11).

**Lines.** A call records the line where its whole expression starts (Lua does the same). `.`
and `[` record their own line.

**Errors that name the opener.** `expect_match(closing, opening, line)` is Lua's
`check_match()`. If the closing token is missing and the opening one is on the **same** line, the
error is the short `')' expected near 'x'`. If it is on an earlier line, the message points back
to it: `')' expected (to close '(' at line 1) near <eof>`. Statements like `if ... end` will use
the same helper.

## Tables

A table constructor is a `TableExpr`, whose fields are `TableField` structs in the field-list
vector. Each field has a `kind`, a `key` and a `value`:

| Source        | Kind         | Key                                   |
| ------------- | ------------ | ------------------------------------- |
| `exp`         | `Positional` | none; it gets the next of 1, 2, 3, ... |
| `name = exp`  | `Named`      | a `StringExpr` with the name: `{x = 1}` is `{["x"] = 1}` |
| `[exp] = exp` | `Keyed`      | the expression                        |

`Named` and `Keyed` mean the same thing; the kind is kept so the tree shows what was written.

`parse_table()` (Lua's `constructor()`) reads `{`, then fields until `}`. After each field a `,`
or `;` allows another one; without a separator the table must end there, so `{1 2}` is
`'}' expected near '2'`. A separator right before `}` is allowed: `{1, 2,}`. The closing `}` uses
`expect_match`, so a table spanning lines reports `(to close '{' at line N)`.

**The two-token lookahead.** In `parse_table_field()`, a field starting with a `Name` is
ambiguous: `{x = 1}` is a named field, but `{x}` and `{x == 1}` are positional ones. The parser
calls `peek()` to look at the token *after* the name: only an `=` makes it a named field. This is
the one place in Lua's grammar that needs it, and the first real use of the parser's lookahead
buffer. A key that is not a plain name, like `{x.y = 1}`, is read as the positional value `x.y`,
and then the `=` fails with `'}' expected near '='`, Lua's own message.

A table is also a call argument: `f{...}` passes exactly one table (G4.10), like `f"..."`.

## The AST printer

`print_ast(ast)` turns a tree into indented text, one node per line, two spaces per level. It is
what `lua2 --ast` prints, and what parser tests compare against, so each part adds its nodes'
lines to it. It visits each node with `std::visit`, so a node type it does not print is a compile
error.

Values are shown the way the debug views show them everywhere (`text_format.hpp`): strings in
double quotes with unprintable bytes escaped (`"a\tb\x00"`), and floats in their shortest exact
form with `.0` when they look like integers (`1000.0`).

Children appear in source order: `Call` prints the function first, then the arguments;
`MethodCall m` prints the object, then the arguments. A `Table` lists its fields as `Item`
(positional, the value below), `Field name` (named, the value below) or `Field` (keyed, the key
then the value below). For `return a.b[c](d):e"f"`:

```
Chunk
  Return
    MethodCall e
      Call
        Index
          Index
            Name a
            String "b"
          Name c
        Name d
      String "f"
```

`lua2 --ast` reaches the internal parser through `lua2::ast_dump(source, chunk_name)` in
`include/lua2/debug.hpp`, the same way `--tokens` uses `token_dump`.

## Not handled yet

- **Functions** (part 7). Until then `function` is `unexpected symbol`.
- **A depth limit** (part 9). Parsing is recursive, so a very deeply nested expression, such as
  thousands of `-` or `..` in a row, can overflow the C++ stack until the limit exists. The
  printer is recursive too.
- **Statements** other than `return` (parts 5 to 8). Until then they end the chunk early, and the
  token is reported as `<eof> expected near 'x'`.
- Scopes and name resolution (parts 9 and 10), the graph view (part 11).
