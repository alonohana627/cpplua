# Parser plan

Roadmap stage 3, cut into small parts. Each part adds a slice of the grammar together with the
AST nodes it produces, its own tests, and its lines in the AST printer, and ends with everything
green (`build`, `ctest`, `tidy`, `format-check`). Rule ids such as `G7.4` point to
[../spec/grammar.md](../spec/grammar.md). You only need the rules listed for the current part.

Background reading is in [../learn/parser.md](../learn/parser.md). How the finished code works
will be recorded in [../final_docs/parser.md](../final_docs/parser.md).

Finishing part 12 completes the stage and bumps the version to 0.0.3.

## Decisions to make in part 0

These are the design questions from the reading notes (section 9). Part 0 settles them before
any grammar is written:

- ~~How the AST holds its nodes~~ **Decided: an arena.** Nodes live in a few `std::vector`s
  owned by one `Ast`, and refer to their children by typed 32-bit indices (`ExprId`, `StatId`).
  Each node is a `std::variant` of plain structs, so passes still get the compiler's
  "every node type handled" check. See [../learn/ast_representation.md](../learn/ast_representation.md).
- ~~How the parser looks ahead~~ **Decided: a one-token buffer in the parser** (`current_` plus
  an optional `lookahead_` filled by `peek()`), like Lua's `luaX_lookahead`. The lexer stays a
  plain stream. Measured on the fixtures: streaming runs at ~218 MB/s, while lexing everything
  into a vector first was ~2.2x slower.
- How errors travel: every parse function returns `std::expected`, like the lexer (already
  decided in the roadmap); what remains is keeping that readable.
- ~~Where token text for `near '...'` comes from~~ **Decided: every `Token` stores its offset
  and length in the source as two `std::uint32_t`** (8 bytes; 40 to 48). The text is
  `source.substr(offset, length)`. Short strings are the one exception: like Lua, their near text
  is the value after escapes, in quotes (G9.2).
- ~~Whether names are resolved while parsing or in a separate pass~~ **Decided: while parsing,
  like Lua's `lparser.c`** (one pass, Lua's exact error order). The scope bookkeeping (blocks,
  locals, labels, upvalues) lives in its own small class that the parser calls, so parse
  functions stay mostly grammar and the scope logic can be tested on its own. Parts 9 and 10
  build it.

## Testing approach

Most parser tests compare a printed tree with the expected text:

```cpp
CHECK(ast_of("return 1 + 2 * 3") == R"(Chunk
  Return
    Binary +
      Integer 1
      Binary *
        Integer 2
        Integer 3
)");
```

So the AST printer behind `lua2 --ast` is built in part 0 and grows with every part. Error tests
compare the exact message, as the lexer tests do.

## Parts

### Part 0: Foundations

- Add each token's source text to `Token` (for `near '...'` in parser errors, G9.2).
- The AST root (`Chunk` holding a `Block`) and the chosen node representation.
- A `Parser` class over the `Lexer`: the current token, one token of lookahead, and the helpers
  every rule uses: `check`, `accept`, `expect` (with Lua's `'X' expected` message).
- Parse an empty chunk; anything else is an error for now.
- The AST printer and `lua2 --ast file.lua`, printing the (empty) tree.
- **Rules**: G2.1, G9.1, G9.2, G9.4.

### Part 1: Simple expressions and `return`

- `nil`, `true`, `false`, numerals, strings, `...` (G4.1). `return` with an expression list
  (G2.3), the first statement that can hold an expression, so every later part can be tested
  through `return <exp>`.
- `return` must be last in its block (G2.5); `unexpected symbol` when no expression starts here
  (G4.7).
- **Rules**: G2.2-G2.5, G4.1, G4.6, G4.7, G9.3 (`unexpected symbol`, `<eof> expected`).

### Part 2: Operators and precedence

- Unary and binary operators with Lua's priority table and associativity, using precedence
  climbing (G7.8).
- **Tests**: every operator; each precedence level against its neighbors; `-x^2`, `2^-3^2`,
  `a..b..c`, `a-b-c`, `not not x`, `a < b < c`.
- **Rules**: G7.1-G7.8.

### Part 3: Names, fields, indexing and calls

- Names, `(exp)` (a `Paren` node, G4.11), `.Name`, `[exp]`, calls with `(args)`, `"str"` and
  `[[str]]` arguments, and method calls `obj:m(args)`, chained in any order (G4.8).
- **Tests**: `a.b[c](d):e"f"`, calls across lines (G3.16), `obj:m` without arguments (G4.9),
  `"s":upper()` as an error and `("s"):upper()` as fine.
- **Rules**: G3.16, G4.2-G4.5, G4.7-G4.11.

### Part 4: Table constructors

- `{}`, positional fields, `Name = exp`, `[exp] = exp`, both separators, a trailing separator.
- The two-token lookahead for `{x = 1}` versus `{x}` (G6.5). Table arguments `f{...}` (G4.10).
- **Rules**: G6.1-G6.7, G4.10.

### Part 5: Statements: calls, assignment and `local`

- Expression statements: a call, or an assignment with one or more targets (G3.1).
- `syntax error` for anything else (G3.8) and for bad targets (G3.9).
- `local` with names, attributes `<const>` and `<close>`, and optional values (G3.2, G3.3).
  `unknown attribute` and `multiple to-be-closed variables` here; the const assignment check
  comes in part 9.
- `;` as an empty statement, and statements without separators (G2.6).
- **Rules**: G2.6, G3.1-G3.3, G3.6, G3.8, G3.9, G3.15 (attribute errors).

### Part 6: Blocks and control flow

- `do ... end`, `while`, `repeat ... until`, `if`/`elseif`/`else`, numeric `for`, generic `for`.
- The `'end' expected (to close 'while' at line N)` message, and its short form on one line.
- `'=' or 'in' expected` for a broken `for` (G3.10).
- **Rules**: G3.1, G3.7, G3.10, G9.3 (`to close` messages).

### Part 7: Functions

- Function expressions, parameter lists with `...` (G5.1-G5.4).
- `function a.b:c() end` statements, with `self` for `:` (G3.5, G3.11).
- `local function f` (G3.12).
- `cannot use '...' outside a vararg function` (G4.12, G2.4).
- **Rules**: G2.4, G3.5, G3.11, G3.12, G4.12, G5.1-G5.5.

### Part 8: `goto`, labels and `break`

- Parse `goto Name`, `::Name::` and `break` into the tree (G3.4, G8.7).
- Their checks need scopes, so they come in part 9.
- **Rules**: G3.1, G3.4, G8.7.

### Part 9: Scopes and compile-time checks

- Track blocks, loops, functions and the locals visible in each.
- `break outside loop`, `no visible label`, `label already defined`, `jumps into the scope of
  local`, including the end-of-block label rule (G8.1-G8.6).
- Local scope starts after the statement (G3.13); the `repeat` condition sees the body's locals
  (G3.14).
- `attempt to assign to const variable` (G3.15).
- A nesting-depth limit with a clear error, and the 200-local limit (G10).
- **Rules**: G3.13-G3.15, G8, G10.

### Part 10: Name resolution

- Mark every name in the tree as a **local** (with its slot), an **upvalue** (with its index in
  the closure) or a **global** (read through `_ENV`), and give every function its list of
  upvalues (G5.6). The interpreter will use these directly.
- The 255-upvalue limit (G10).
- **Rules**: G5.6, G10.

### Part 11: The AST as a graph

- `lua2 --ast=dot file.lua` writes Graphviz DOT, to render the tree as a picture with
  `dot -Tsvg tree.dot -o tree.svg`.
- **Tests**: the DOT output of a small program.

### Part 12: Conformance sweep

- Parse every file of the official test suite in `tests/fixtures/lua-5.4.9-tests/`, as a
  `[conformance]` test: no errors, and spot-check a few trees.
- Any surprise becomes a new test. Bump the version to 0.0.3. The parser is done.

## How we work through a part

Same as the lexer: you name the part, it is implemented with its tests, its section of
`docs/final_docs/parser.md` is written, and nothing is committed until you approve it.
