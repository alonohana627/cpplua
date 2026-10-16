# The parser

How the parser works today. This page describes the code as it is, and is updated as the parser
grows. The rules it implements are in [../spec/grammar.md](../spec/grammar.md), and the remaining
work is in [../plan/parser.md](../plan/parser.md).

**Status:** parts 0 to 11 of the plan: **the whole Lua 5.4 grammar parses.** A chunk is a list
of statements: function calls, assignments, `local` declarations (with `<const>` and
`<close>`), empty `;` statements, `do`, `while`, `repeat`, `if`/`elseif`/`else`, numeric and
generic `for`, `function` statements, `local function`, `goto`, labels, `break`, and a final
`return`. Every expression parses, with every operator at Lua's precedence. The checks that
need scopes are in place (part 9): `break` and `goto` targets, duplicate labels, jumps into a
local's scope, `<const>` assignment, the 200-local limit and a nesting limit. Every name in the
tree records whether it is a local, an upvalue or a global, and every function lists its
upvalues (part 10), and `lua2 --ast=dot` draws the tree as a graph (part 11). Still to come:
the official test suite (part 12).

## Where it lives

| File                                  | Contents                                          |
| ------------------------------------- | ------------------------------------------------- |
| `lib/internal/parser/ast.hpp`         | the tree: ids, ranges, node structs, `Ast`        |
| `lib/parser/ast.cpp`                  | its implementation                                |
| `lib/internal/parser/parser.hpp`      | the `Parser` class                                |
| `lib/parser/parser.cpp`               | its implementation                                |
| `lib/internal/parser/scopes.hpp`      | `Scopes`: scopes, compile-time checks, name resolution |
| `lib/parser/scopes.cpp`               | its implementation                                |
| `lib/internal/parser/ast_printer.hpp` | `print_ast()`, the tree as indented text, and `print_ast_dot()`, as a graph |
| `lib/parser/ast_printer.cpp`          | its implementation                                |
| `include/lua2/debug.hpp`              | `ast_dump()` and `ast_dot_dump()`, public, for `lua2 --ast` and `--ast=dot` |
| `lib/internal/overloaded.hpp`         | `Overloaded`, for `std::visit` with lambdas       |
| `lib/internal/text_format.hpp`        | `quoted()`, `format_float()`, shared by the debug views |
| `tests/parser_test.cpp`               | parser tests, tagged `[parser]` (and `[ast]`, `[resolve]`) |

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
| `Expr`  | `NilExpr`, `TrueExpr`, `FalseExpr`, `IntegerExpr{value}`, `FloatExpr{value}`, `StringExpr{text}`, `VarargExpr`, `UnaryExpr{op, operand}`, `BinaryExpr{op, left, right}`, `NameExpr{name, variable, environment}`, `ParenExpr{inner}`, `IndexExpr{object, key}`, `CallExpr{function, arguments}`, `MethodCallExpr{object, method, arguments}`, `TableExpr{fields}`, `FunctionExpr{function}` |
| `Stat`  | `ReturnStat{values}`, `CallStat{call}`, `AssignStat{targets, values}`, `LocalStat{names, values}`, `DoStat{body}`, `WhileStat{condition, body}`, `RepeatStat{body, condition}`, `IfStat{clauses, else_body, has_else}`, `NumericForStat{variable, start, limit, step, has_step, body}`, `GenericForStat{names, values, body}`, `FunctionStat{target, function, is_method}`, `LocalFunctionStat{name, function}`, `GotoStat{label}`, `LabelStat{name}`, `BreakStat`; lists are `IdRange`s, bodies are `Block`s |
| -       | `Function{parameters, is_vararg, body, line, end_line, upvalues}`: function bodies, in their own vector, referred to by `FunctionId`; the main chunk is one too |
| -       | `Upvalue{name, from_parent_local, index}`: one entry of a function's upvalue list, read with `upvalues(range)` |

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
  `IdRange<FieldId>` ranges. The names of a `local` statement work the same way, as
  `LocalName{name, attribute}` structs: `add_local_name_list(names)` and `local_names(range)`
  (a generic `for` reuses them for its loop variables). The clauses of an `if` are
  `IfClause{condition, body}` structs: `add_if_clause_list(clauses)` and `if_clauses(range)`.
- **Bodies are embedded.** A `Block` is only an `IdRange<StatId>`, 8 bytes, so loops and
  branches hold their bodies directly (`WhileStat{condition, body}`) instead of through another
  id.
- **Functions have their own vector.** A function body is a `Function` struct in
  `functions_`, added with `add_function()` and read with `function(id)`. Function expressions
  and both function statements refer to it by `FunctionId`. The main chunk is a `Function` too
  (`main_function()`; `root()` is its body), with `is_vararg` set and `_ENV` as its one upvalue,
  like Lua's main function. The interpreter will turn each `Function` into a prototype.

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
| `parse_block()`           | `block ::= { stat } [ retstat ]` (G2.2)  | statements until `block_follow()` or `return` |
| `parse_statement()`       | `stat` (G3.1)                            | dispatches on the first token; `;` gives no node |
| `parse_expression_statement()` | a call, or `varlist = explist` (G3.1) | Lua's `exprstat()`, see "Statements" |
| `parse_local()`           | `'local' attnamelist [ '=' explist ]` (G3.1) | Lua's `localstat()` |
| `parse_attribute()`       | `attrib ::= [ '<' Name '>' ]` (G3.3)     | `<const>`, `<close>`, or `unknown attribute` |
| `parse_do()`, `parse_while()`, `parse_repeat()`, `parse_if()` | the control statements of G3.1 | see "Control flow" |
| `parse_for()`             | `for Name` then `=` or `,`/`in` (G3.10)  | dispatches to `parse_numeric_for()` or `parse_generic_for()` |
| `parse_for_body(line)`    | `'do' block 'end'`                       | shared by both fors                       |
| `parse_function_body(line, is_method)` | `funcbody ::= '(' [parlist] ')' block 'end'` (G5.2) | see "Functions" |
| `parse_function_statement()` | `'function' funcname funcbody` (G3.5)  | builds the target like a variable |
| `parse_local_function(line)` | `'local' 'function' Name funcbody` (G3.12) | called by `parse_local()` |
| `parse_jump()`            | `'goto' Name`, `'::' Name '::'`, `'break'` (G3.1, G3.4) | see "goto, labels and break" |
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

## Statements

`parse_block()` reads statements in a loop until the block ends (`block_follow()`) or a
`return` comes. Statements need no separator: `a = 1 b = 2` is two statements, and `;` is an
empty statement that `parse_statement()` skips without adding a node.

### Calls and assignments

A statement that does not start with a keyword starts with an expression, and
`parse_expression_statement()` (Lua's `exprstat()`) reads it as a suffixed expression, then
decides from the next token:

- **`=` or `,`**: an assignment. It collects targets separated by commas, then `=`, then the
  values, giving an `AssignStat{targets, values}`. Each target must be a *variable*, a
  `NameExpr` or an `IndexExpr`; anything else is `syntax error` (G3.9). Like Lua, the check runs
  once the token after the target has been read, so `f() = 1` reports `syntax error near '='`.
- **Anything else**: it must be a call (`CallExpr` or `MethodCallExpr`), which becomes a
  `CallStat`. Any other expression would do nothing, so `x` or `a.b` alone is
  `syntax error` (G3.8).

Because the statement starts with a *suffixed* expression, it cannot start with a literal or an
operator: `1 + 2` as a statement is `unexpected symbol near '1'`. And `(f())` is a parenthesized
expression, not a call, so it is `syntax error` too.

### `local`

`parse_local()` (Lua's `localstat()`) reads names, each with an optional attribute, then an
optional `=` and values, giving a `LocalStat{names, values}`. No `=` means no values.

`parse_attribute()` reads `<name>`:

- `<const>`: the variable cannot be assigned to (checked in part 9);
- `<close>`: its value is closed when the variable goes out of scope;
- anything else: `unknown attribute 'foo'`.

At most one `<close>` per statement: a second one is `multiple to-be-closed variables in local
list`.

Watch out for maximal munch here: in `local x<const>=1` the lexer reads `>=` as one token, so
real Lua fails with `'>' expected near '>='`, and so do we. With a space, `<const> =1` works.

### Semantic errors

The attribute errors are *semantic* errors: about meaning, not grammar. Like Lua's
`luaK_semerror()`, they leave out the `near` part (G9.5):

```
input:1: unknown attribute 'foo'
input:1: multiple to-be-closed variables in local list
```

`semantic_error(message)` builds them, by passing an empty `near` to `make_syntax_error`, which
then omits that part. The `goto`, label, `break` and `<const>` checks of part 9 will use it too.

## Functions

All three forms share one body parser, `parse_function_body(line, is_method)`:

1. **Parameters** (G5.3): names separated by commas, optionally ending with `...`, which sets
   `is_vararg`. `...` must be last, so `(..., a)` is `')' expected near ','`. Anything else in
   the list is `<name> or '...' expected` (G5.4).
2. **Body**: a block, then the `end` (with `expect_match`, so `(to close 'function' at line N)`
   when it is missing). The function records the line of `function` and of its `end`.

The three forms:

| Source                         | Node                                                     |
| ------------------------------ | -------------------------------------------------------- |
| `function (params) ... end` as an expression | `FunctionExpr{function}`                   |
| `function a.b.c(params) ... end` | `FunctionStat{target, function, is_method = false}` |
| `function a.b:m(params) ... end` | `FunctionStat{target, function, is_method = true}`, with `self` added as the first parameter |
| `local function f(params) ... end` | `LocalFunctionStat{name, function}`                  |

- **The target of a function statement** is built exactly like a variable in an expression:
  `function a.b.c()` gives the target `a["b"]["c"]`, as `NameExpr` and `IndexExpr` nodes, which
  is what the interpreter will assign to. After `:`, only the method name may follow, so
  `function a:b.c()` is `'(' expected near '.'`, Lua's message.
- **Methods** (G3.11): `function obj:m(x)` is `function obj.m(self, x)`. The parser adds `self`
  to the parameter list itself, so the rest of the interpreter never needs to know.
- **`local function f`** is a separate statement from `local f = function() end`, because they
  differ in scope (G3.12): in the first, `f` already exists inside its own body, so it can call
  itself. That matters in part 10.
- **A function expression cannot be called directly**, like a literal: it is a simple
  expression, not a prefix expression. `function() end ()` leaves the `(` over;
  `(function() end)()` works.

### `...` only in vararg functions

`...` is the extra arguments of a vararg function, so using it anywhere else is an error
(G4.12): `cannot use '...' outside a vararg function near '...'`. The parser keeps
`vararg_functions_`, a stack with one entry per function being parsed: the main chunk's `true`
at the bottom (the main chunk is vararg, G2.4), and each function body pushes its own
`is_vararg` and pops it when it ends. `...` checks the top. So a non-vararg function nested in a
vararg one cannot use the outer `...`, and once it ends, the outer function can again.

## `goto`, labels and `break`

`parse_jump()` reads the three jump statements, since they share their shape:

| Source        | Node              |
| ------------- | ----------------- |
| `goto name`   | `GotoStat{label}` |
| `::name::`    | `LabelStat{name}` |
| `break`       | `BreakStat`       |

Unlike `return`, these may appear anywhere in a block (G8.7): `while x do break f() end` is
valid, even though `f()` can never run.

Whether a `goto` has a visible label, whether a label is defined twice, whether a `goto` jumps
into a local's scope, and whether a `break` is inside a loop all depend on the scopes around the
statement; see "Scopes and compile-time checks".

## Scopes and compile-time checks

Some errors are not about grammar but about what is visible where (G3.13-G3.15, G8, G10). The
parser tracks that with a `Scopes` object (`lib/internal/parser/scopes.hpp`), a separate class so
that the parse functions stay mostly grammar. The parser calls it at exactly the points where
Lua's `lparser.c` calls `open_func`, `enterblock`, `new_localvar`, `adjustlocalvars`,
`createlabel` and friends, so every check fails at the same moment, with the same message, as in
Lua. The errors are semantic errors, without `near` (G9.5).

### What it tracks

| State          | Holds                                                                       |
| -------------- | --------------------------------------------------------------------------- |
| `functions_`   | one entry per function being parsed (the main chunk first): its line, where its locals and labels start, how many locals are visible, and its blocks |
| `variables_`   | every declared local of every open function, with its attribute             |
| `labels_`      | every visible label, with its line and how many locals were visible at it   |
| `gotos_`       | every `goto` and `break` still waiting for its target, with how many locals were visible at it |

**Declared vs visible.** A local is *declared* when its name is read but becomes *visible* only
when `activate()` is called, which is after the whole statement: so in `local x = x`, the right
side means the outer `x` (G3.13). `local function f` activates `f` before its body, so the
function can call itself; `local f = function() ... end` does not (G3.12). Function parameters
are visible in the body; loop variables only in the loop body.

**Blocks.** `parse_scoped_block()` opens a block, parses, and closes it; closing removes the
block's locals and labels. The scopes follow Lua's layout exactly: a loop is a *loop block* (what
`break` jumps to) around the body; `repeat` keeps its body's block open through the `until`
condition, so the condition sees the body's locals (G3.14); a `for` has a loop block holding its
hidden control variables, and a block for the loop variables around the body.

### The checks

- **`break`** (G8.6) is recorded as pending, and every loop block resolves the breaks inside it
  when it closes. One left when the function ends is `break outside loop at line N`. Like Lua,
  that is reported at the end of the function, after its `end` (or at the end of the chunk).
- **`goto`** (G8.2): if its label is already visible, it is a backward jump and always fine.
  Otherwise it waits. When a block closes, its waiting gotos move out to the enclosing block
  (now "outside" the block's locals); when the function ends, the first one still waiting is
  `no visible label 'x' for <goto> at line N`. Labels of other functions are never visible.
- **Labels** (G8.1, G8.3): a label visible where another with the same name is defined is
  `label 'x' already defined on line N`. Creating a label resolves the waiting gotos of its
  block.
- **Jumping into a scope** (G8.4): a waiting goto that saw fewer locals than its label sees
  would skip a local's declaration: `<goto x> at line N jumps into the scope of local 'y'`,
  naming the first local it would skip.
- **Labels at the end of a block** (G8.5): a label followed only by the end of its block (and
  maybe `;` or more labels) counts as outside the block's locals, so the usual
  `goto continue ... ::continue:: end` works. Lua decides this after skipping the `;` and labels
  that follow, and registers consecutive labels last-first; `parse_block()` reproduces that by
  holding labels back until the next real statement, then registering them in reverse. Before
  `until` a label is *not* at the end, because the condition still sees the locals.
- **`<const>` and `<close>`** (G3.15): assigning to a name that `lookup()` finds as such a local
  (in this function or, as an upvalue, an enclosing one) is
  `attempt to assign to const variable 'x'`. Fields are fine (`t.x = 1`), and so is a shadowing
  local. `function x() end` assigns too, and is checked after its body, like Lua.
- **200 locals per function** (G10): `too many local variables (limit is 200) in main function`
  (or `in function at line N`), a normal syntax error with `near`. Like Lua, each `for` counts
  its hidden control variables too (3 or 4, named `(for state)`), and locals of finished blocks
  stop counting.

### The nesting limit

The parser is recursive, so very deep nesting would overflow the C++ stack. Like Lua's
`enterlevel`, every statement and every subexpression counts one level while it is being parsed
(the `DepthGuard` class), and more than 200 levels is the error `C stack overflow`. Long chains
of left-associative operators (`1 + 1 + ... + 1`) are a loop, not recursion, so they have no
limit. (Real Lua reports this error without a chunk name and line, because it is raised as a
runtime error; ours has both.)

### After an error

Parsing stops at the first error, and the scopes are left as they are: `parse_scoped_block()`
only closes its block when the block parsed successfully, because closing blocks in the middle
of an unfinished construct would not match what was opened.

## Name resolution

Every `NameExpr` records what its name means, in a `VariableRef{kind, index}` (G5.6):

| `kind`    | `index`                                         | Printed as           |
| --------- | ----------------------------------------------- | -------------------- |
| `Local`   | the local's slot in its function                | `Name x (local 2)`   |
| `Upvalue` | its position in the function's upvalue list     | `Name x (upvalue 0)` |
| `Global`  | unused                                          | `Name x`             |

A global `x` means `_ENV.x`, so a global's `NameExpr` also resolves `_ENV` into `environment`:
upvalue 0 in the main chunk, the function's own `_ENV` upvalue in a nested function, or a local
after `local _ENV = ...`.

The parser calls `Scopes::resolve(name)` as soon as it reads a name (in
`parse_primary_expression()` and for the first name of a `function` statement), through
`make_name()`. `resolve_in()` works exactly like Lua's `singlevaraux`:

1. a visible local of the current function, the most recent first: its slot is its position
   among the function's declared locals, so finished blocks give their slots back, and a `for`
   loop's hidden variables take slots before the loop variables (`for i` puts `i` in slot 3);
2. an upvalue the function already has, so a name used many times is captured once;
3. at the main chunk, nothing more: a global;
4. otherwise the same search in the enclosing function. If found there, it becomes a new
   upvalue here: `Upvalue{name, from_parent_local, index}`, where `from_parent_local` says whether
   `index` is a local slot of the enclosing function or one of its upvalues. A name several
   functions deep is added to every function in between.

The main chunk starts with `_ENV` as upvalue 0 (`open_main_function()`). When a function closes,
`close_function()` hands back its upvalue list, which the parser stores in the `Function`.

More than 255 upvalues in one function (G10) is
`too many upvalues (limit is 255) in function at line N`, a normal syntax error with `near`.
Like Lua, the name is resolved after it is read, so `near` shows the token after the name.

One difference: Lua turns a `<const>` local with a constant value into a compile-time constant
that is never captured, so it uses no upvalue. Ours treats `<const>` locals like any other, so
they become upvalues as usual. The limit can only be hit slightly sooner than in Lua.

## Control flow

Each control statement is its keyword, its parts, and a block, read directly from the grammar:

| Statement                                      | Node                                     |
| ---------------------------------------------- | ---------------------------------------- |
| `do block end`                                 | `DoStat{body}`                           |
| `while exp do block end`                       | `WhileStat{condition, body}`             |
| `repeat block until exp`                       | `RepeatStat{body, condition}`            |
| `if exp then block {elseif exp then block} [else block] end` | `IfStat{clauses, else_body, has_else}` |
| `for Name = exp, exp [, exp] do block end`     | `NumericForStat{variable, start, limit, step, has_step, body}` |
| `for namelist in explist do block end`         | `GenericForStat{names, values, body}`    |

A few details:

- **Where a block ends.** `parse_block()` stops at `block_follow()`: `end`, `else`, `elseif`,
  `until` or the end of input. The statement that called it then checks that the right word is
  there. So `if x then else elseif y then end` fails with `'end' expected near 'elseif'`: after an
  `else`, only `end` may come.
- **Missing closing words.** Every closing `end` or `until` uses `expect_match()` with the line of
  the opening keyword, so a block left open across lines reports its opener, like Lua:
  `'end' expected (to close 'while' at line 2) near <eof>`. Within one line it is just
  `'end' expected near <eof>`. The middle words (`do`, `then`, `in`, the `,` of a numeric for)
  use plain `expect()`: `'then' expected near 'f'`.
- **`if` clauses.** The `if` and each `elseif` become `IfClause{condition, body}` entries, in
  order. `has_else` records whether there was an `else`, even an empty one, so the tree shows
  what was written.
- **Two kinds of `for`.** `parse_for()` reads `for Name`, then the next token decides (G3.10):
  `=` is a numeric for; `,` or `in` is a generic one; anything else is
  `'=' or 'in' expected`. The numeric step is optional (`has_step`). The generic for's names
  reuse the `LocalName` list (without attributes), because they are new locals of the loop body.
- **`return` inside blocks.** `return` must end its *own* block (G2.5), so
  `if x then return 1 else return 2 end` is fine, and so is `repeat return until x`, because
  `until` ends a block too. `do return 1 x = 2 end` fails with `'end' expected near 'x'`.

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
lines to it.

Inside, the `Printer` class has one `print(const XStat&, int depth)` or
`print(const XExpr&, int depth)` overload per node type, each a few lines long. They are
dispatched with one generic lambda:

```cpp
std::visit([&](const auto& node) { print(node, depth); }, ast_.statement(id));
```

If a node type has no `print` overload, that call does not compile, so the printer can never
silently skip a node, and adding a node type means adding one small function rather than growing
one big one.

**The printer does not recurse.** A tree can be much deeper than the C++ stack allows:
`1 + 1 + ... + 1` with ten thousand terms is ten thousand `Binary` nodes deep on its left side,
and the parser accepts it (it is a loop there). A recursive printer crashed on it. So each
`print()` writes only the node's **own** line, and *schedules* everything below it (children,
and headings like `Body`) with `expression()`, `statement()`, `heading()` and friends.
`run()` keeps a stack of scheduled items and works through them in order: after each `print()`,
the items it scheduled are pushed in reverse, so the first one comes off next. The output is the
same as a recursive walk, at any depth.

Values are shown the way the debug views show them everywhere (`text_format.hpp`): strings in
double quotes with unprintable bytes escaped (`"a\tb\x00"`), and floats in their shortest exact
form with `.0` when they look like integers (`1000.0`).

Children appear in source order: `Call` prints the function first, then the arguments;
`MethodCall m` prints the object, then the arguments. A `Table` lists its fields as `Item`
(positional, the value below), `Field name` (named, the value below) or `Field` (keyed, the key
then the value below). Statements print as `CallStatement` (the call below), `Assign` (with
`Targets` and `Values` sub-headings, since the two lists would otherwise run together), and
`Local a, b <const>` (the names on one line with their attributes, the values below). Loops print
their condition or range first and their statements under a `Body` heading; `Repeat` puts its
condition under `Until` after the body; an `If` lists each clause as `If`/`ElseIf` with the
condition and a `Then` heading, then an optional `Else`. A function prints as
`Function(a, b, ...)`, with `[upvalues: x, _ENV]` added on the same line when it has upvalues,
and its statements below; a `FunctionStatement` (marked `(method)` for `:`) prints its target, then the function; `LocalFunction f` prints the function below. For
`return a.b[c](d):e"f"`:

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

### The graph view

`print_ast_dot(ast)` gives the same tree as a Graphviz DOT graph, and `lua2 --ast=dot`
(through `ast_dot_dump()`) prints it. `dot -Tsvg tree.dot -o tree.svg` draws it as a picture.
For `return 1`:

```
digraph ast {
  ordering=out;
  node [shape=box, fontname="monospace"];
  n0 [label="Chunk"];
  n1 [label="Return"];
  n0 -> n1;
  n2 [label="Integer 1"];
  n1 -> n2;
}
```

It is built from `print_ast()`'s text, not from the tree: each line becomes a box labeled with
the line's text, and its parent is the closest line above it with one level less indentation.
A stack of the open ancestors, one per depth, finds it in one pass. So the two views can never
disagree, a new node type needs only its `print()` overload, and the graph writer is as
non-recursive as the printer. Labels escape `"` and `\` for DOT, and `ordering=out` keeps
children left to right in source order.

## Not handled yet

- **A difference in the nesting limit**: Lua reads a multiple assignment `a, b, c = ...`
  recursively, one level per target, so more than about 200 targets is `C stack overflow`
  there. Ours reads them in a loop and accepts any number. The official test suite checks this
  (`errors.lua`), so part 12 may need to count them.

