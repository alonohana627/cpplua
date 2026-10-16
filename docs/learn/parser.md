# Parsers and ASTs: what they are and what ours must do

Reading material for roadmap stage 3. Nothing here is code we have written yet. The exact rules
are in [../spec/grammar.md](../spec/grammar.md); this page explains the ideas behind them.

## 1. Where the parser sits

```
source text --lexer--> tokens --parser--> AST --interpreter--> result
```

The lexer answered "what are the words?". The parser answers "how do the words fit together?".
It reads tokens one at a time and checks them against Lua's **grammar**, and while it checks, it
builds a tree that describes the program: the **AST** (abstract syntax tree).

For `local x = 1 + 2 * y`, the tokens are a flat list:

```
Local  Name(x)  Assign  Integer(1)  Plus  Integer(2)  Star  Name(y)
```

and the AST is a tree:

```
LocalStatement
├── names: x
└── values:
    └── Binary +
        ├── Integer 1
        └── Binary *
            ├── Integer 2
            └── Name y
```

The tree has already decided that `*` happens before `+`. The interpreter never has to think
about precedence, parentheses or semicolons again: it just walks the tree.

## 2. AST, not parse tree

A *parse tree* (or concrete syntax tree) keeps every token, including `(`, `)`, `then`, `end`
and `,`. An *abstract* syntax tree keeps only what matters for meaning:

- `if x then y() end` becomes `If{condition: x, then: [Call y]}`. The words `then` and `end`
  are gone; they only told the parser where the parts begin and end.
- `(a + b) * c` becomes `Binary *(Binary +(a, b), c)`. The tree shape replaces the
  parentheses.

One Lua exception: `(f())` keeps a `Paren` node, because in Lua parentheses change the meaning
(they cut a call down to one value). See G4.11.

## 3. Grammars

A grammar is a set of rules, each saying what one construct is made of:

```
stat ::= 'while' exp 'do' block 'end'
```

reads: "a `while` statement is the word `while`, an expression, the word `do`, a block, and the
word `end`". Rules refer to each other (`exp`, `block`), and can refer to themselves: a block
contains statements, and a `while` statement contains a block. That self-reference is why a
program is a *tree*.

The notation is called **BNF** (Backus-Naur form), or EBNF with the extras `{ x }` (repeat) and
`[ x ]` (optional). Lua's whole grammar fits on one page: §9 of the manual, restated in
[../spec/grammar.md](../spec/grammar.md).

## 4. Recursive descent

The simplest way to write a parser by hand, and what Lua itself does: **one function per grammar
rule**. Each function looks at the current token, decides which alternative applies, and calls
the functions for the parts:

```
parse_while():
    expect('while')
    condition = parse_exp()
    expect('do')
    body = parse_block()
    expect('end')
    return WhileStatement{condition, body}
```

- **Looking at the current token** chooses the rule: a statement starting with `while` must be
  a `while` loop. Lua's grammar is designed so that one token of lookahead almost always decides.
  The one place that needs two is a table field: in `{x = 1}` versus `{x}`, the parser must see
  the token *after* `x` (G6.5).
- **`expect(token)`** checks the current token and moves past it, or reports
  `'do' expected near ...`. Most syntax errors come from this one helper.
- The call stack of these functions mirrors the shape of the tree being built, hence
  *recursive* descent.

## 5. Expressions and precedence

Statements are easy because each starts with a keyword. Expressions are harder: in
`1 + 2 * 3 - 4`, the parser must decide how the operators group.

A naive rule like `exp ::= exp binop exp` is ambiguous: it allows both `(1 + 2) * 3` and
`1 + (2 * 3)`. Real parsers resolve this with **precedence** (which operator binds tighter) and
**associativity** (how equal operators group: `a - b - c` is `(a - b) - c`, but `a .. b .. c`
is `a .. (b .. c)`).

The classic technique, and the one `lparser.c` uses, is **precedence climbing** (closely related
to a **Pratt parser**). One function parses an expression "with a limit":

1. Read one operand: a unary operator and *its* operand, or a simple expression.
2. While the next token is a binary operator that binds tighter than the limit: consume it,
   and parse the right-hand side with the operator's own priority as the new limit.

Each operator has a left and a right priority (G7.8). Giving an operator a slightly lower right
priority makes it right-associative. This is a dozen lines of code that handles all twelve
levels, and it is worth tracing by hand on `1 + 2 * 3 - 4` and on `2 ^ 3 ^ 2` before writing it.

## 6. Representing the tree in C++

Each node type is a struct (`Binary{op, left, right}`, `If{...}`). The design question is how
the parts point to each other, because a node holds other nodes of *different* types. Common
choices:

- **A class hierarchy**: `struct Expr { virtual ~Expr(); }`, with `struct Binary : Expr`, held
  through `std::unique_ptr<Expr>`. Familiar, but every use needs a cast or a virtual "visitor".
- **`std::variant`**: `using Expr = std::variant<Nil, Integer, Binary, Call, ...>`, where
  `Binary` holds `std::unique_ptr<Expr>` children (a variant cannot contain itself directly).
  Code switches on the alternatives with `std::visit`, and the compiler can check that every
  node type is handled.
- **An arena with indices**: all nodes live in one `std::vector`, and children are referred to
  by index. Very fast and cache-friendly, but more bookkeeping.

There is no wrong answer; part 0 of the plan picks one, and the AST printer (`lua2 --ast`) is
a good first test of whichever we pick, because it must visit every node type.

## 7. More than syntax: scopes

Lua's parser does a little more than check grammar, and so will ours:

- **Scopes.** It tracks which local variables are visible where, so that it can tell for every
  name whether it is a **local**, an **upvalue** (a local of an enclosing function, captured by a
  closure) or a **global**. This is called *name resolution*.
- **Checks that need scopes**: `break` outside a loop, `goto` to a missing label, a `goto` that
  jumps into a local's scope, assigning to a `<const>` variable, `...` outside a vararg
  function (G8, G3.15, G4.12).

These are still compile-time errors in Lua, reported with the same `chunk:line: message` format
as syntax errors.

## 8. Errors

Like the lexer, the parser stops at the first error, and matches Lua's messages exactly
(G9.3), because Lua programs and the official test suite check them. The most common shapes:

```
input:1: '=' expected near 'y'
input:1: unexpected symbol near '+'
input:5: 'end' expected (to close 'function' at line 3) near <eof>
```

The `near` part names the current token *as written in the source*, so the parser needs each
token's original text, not only its value: `'0x10'`, not `16`.

## 9. Design questions for our parser

Think about these before part 0. We will choose together:

1. **How to hold the tree**: class hierarchy, `std::variant`, or an arena (section 6)?
2. **Lookahead**: the parser needs the current token and sometimes the next one (G6.5). Keep a
   one-token buffer, or let the lexer peek?
3. **Errors**: every parse function returns `std::expected<Node, SyntaxError>`, as the lexer
   does. How do we keep the propagation readable when almost every line can fail?
4. **Token text for errors**: add the token's source text (a `std::string_view` into the source)
   to `Token`, or rebuild it when an error happens?
5. **Name resolution**: resolve locals, upvalues and globals while parsing (like Lua, one pass),
   or in a separate pass over the finished tree (simpler to read and test)?

## 10. Reading list

In this order. The first three are enough to start.

1. **Crafting Interpreters, chapter 5 "Representing Code"** (craftinginterpreters.com): what an
   AST is and how to print one.
2. **Crafting Interpreters, chapter 6 "Parsing Expressions"**: recursive descent and precedence,
   with a small grammar.
3. **Lua 5.4 Reference Manual §3.3-§3.4 and §9** (lua.org/manual/5.4/manual.html): what each
   statement and expression means, and the complete syntax.
4. **"Pratt Parsers: Expression Parsing Made Easy"** by Bob Nystrom (journal.stuffwithstuff.com,
   2011): the operator-priority idea behind section 5, explained step by step.
5. **Crafting Interpreters, chapters 8 "Statements and State" and 11 "Resolving and Binding"**:
   statements, blocks, scopes, and resolving names in a separate pass.
6. **`lparser.c` in the Lua 5.4 source** (github.com/lua/lua): the real thing, about 2000 lines.
   It parses and generates bytecode in the same pass, which makes it dense. Read `statement()`,
   `subexpr()`, `primaryexp()`, `suffixedexp()` and `constructor()` *after* writing your own
   versions, to compare.

## 11. Check yourself

You are ready for part 0 when you can answer these:

- Draw the AST of `a = b + c * d ^ e ^ f`.
- Why is `-2^2` equal to `-4`, not `4`?
- What is the difference between `f()` and `(f())` as the last value of `return`?
- Why must the parser look two tokens ahead in `{x = 1}` but not in `{x}`?
- In `local x = x`, which `x` does the right side mean?
- Is `x` alone a valid statement? Why not?

Answers: (1) `Assign{a, Binary +(b, Binary *(c, Binary ^(d, Binary ^(e, f))))}`;
(2) `^` binds tighter than unary minus, so it is `-(2^2)`; (3) `f()` returns all of `f`'s
values, `(f())` only the first; (4) after `x` it must see whether `=` follows to know if `x` is a
key or a value; (5) the outer one, because the new local's scope starts after the statement;
(6) no: a statement made of an expression must be a call or an assignment, so it is
`syntax error`.
