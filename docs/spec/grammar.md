# Lua 5.4 grammar

The complete specification our parser implements. It is based on the Lua 5.4 Reference Manual
§3 (statements and expressions) and §9 (the complete syntax), restated here in our own notation.
Where the manual is silent, the behavior of the reference implementation (`lparser.c` in Lua 5.4)
is the tie-breaker, and those rules are marked **(impl)**.

Every rule has an id like `G4.2`. Tests should name the rule they check, for example
`TEST_CASE("G7.3 ^ is right-associative", "[parser]")`.

The parser reads the tokens described in [lexical.md](lexical.md).

---

## G1. Notation

- `{ x }` means zero or more `x`. `[ x ]` means an optional `x`. `a | b` means `a` or `b`.
- Quoted text such as `'='` or `'end'` is a token. `Name`, `Numeral` and `LiteralString` are the
  lexer's `Name`, `Integer`/`Float` and `String` tokens.
- A *block* is a list of statements. A *chunk* is the block of a whole file or string.

## G2. Chunks and blocks

```
G2.1  chunk     ::= block <eof>
G2.2  block     ::= { stat } [ retstat ]
G2.3  retstat   ::= 'return' [ explist ] [ ';' ]
```

- **G2.4** A chunk is compiled as the body of an anonymous function that takes `...` (it is a
  vararg function), so `...` may be used at the top level.
- **G2.5** `return` can only be the **last** statement of a block (optionally followed by one
  `;`). Anything after it is an error: in the main chunk `<eof> expected near 'x'`, inside a
  block `'end' expected near 'x'` (or the block's own closing word). **(impl)**
- **G2.6** Statements need no separator: `a = 1 b = 2` is two statements. `;` is an empty
  statement and may appear anywhere a statement can.

## G3. Statements

```
G3.1  stat ::= ';'
             | varlist '=' explist
             | functioncall
             | label
             | 'break'
             | 'goto' Name
             | 'do' block 'end'
             | 'while' exp 'do' block 'end'
             | 'repeat' block 'until' exp
             | 'if' exp 'then' block { 'elseif' exp 'then' block } [ 'else' block ] 'end'
             | 'for' Name '=' exp ',' exp [ ',' exp ] 'do' block 'end'
             | 'for' namelist 'in' explist 'do' block 'end'
             | 'function' funcname funcbody
             | 'local' 'function' Name funcbody
             | 'local' attnamelist [ '=' explist ]

G3.2  attnamelist ::= Name attrib { ',' Name attrib }
G3.3  attrib      ::= [ '<' Name '>' ]
G3.4  label       ::= '::' Name '::'
G3.5  funcname    ::= Name { '.' Name } [ ':' Name ]
G3.6  varlist     ::= var { ',' var }
G3.7  namelist    ::= Name { ',' Name }
```

Rules the grammar alone does not show:

- **G3.8 Expression statements.** A statement that starts with an expression must be either a
  function call or an assignment. `x`, `1 + 2` or `(f)` alone is `syntax error`. **(impl)**
- **G3.9 Assignment targets.** Every target in a `varlist` must be a variable (a name, `t[k]` or
  `t.k`). `f() = 1` and `(a) = 1` are `syntax error`. **(impl)**
- **G3.10 Numeric `for`.** `for i = a, b, c` takes exactly two or three expressions after `=`.
  After `for Name`, the next token must be `=`, `,` or `in`, otherwise
  `'=' or 'in' expected`. **(impl)**
- **G3.11 `function` statements.** `function a.b.c:m() ... end` assigns to `a.b.c.m`. The `:`
  form adds a first parameter named `self`. `function f() end` assigns to the variable `f`
  (local if one is visible, otherwise global).
- **G3.12 `local function f`** declares the local `f` **before** the body, so the function can
  call itself recursively. `local f = function() ... end` declares `f` after, so inside the body
  `f` means an outer variable.
- **G3.13 Local scope.** The variables of `local` come into scope **after** the whole statement:
  in `local x = x`, the right-hand `x` is the outer one.
- **G3.14 `repeat ... until exp`.** The condition is inside the loop's scope: it can see locals
  declared in the body.
- **G3.15 Attributes.** The only attributes are `<const>` and `<close>`. Any other name is
  `unknown attribute 'x'`. At most one variable in a `local` statement may be `<close>`, otherwise
  `multiple to-be-closed variables in local list`. A `<const>` or `<close>` variable cannot be
  assigned to: `attempt to assign to const variable 'x'`. **(impl)**
- **G3.16 Calls across lines.** A call's `(` may be on the line after the function expression:
  `f\n(x)` is a call, not two statements. (Lua 5.1 called this ambiguous; 5.4 does not.)

## G4. Expressions

```
G4.1  exp ::= 'nil' | 'false' | 'true'
            | Numeral | LiteralString | '...'
            | functiondef
            | prefixexp
            | tableconstructor
            | exp binop exp
            | unop exp

G4.2  prefixexp    ::= var | functioncall | '(' exp ')'
G4.3  var          ::= Name | prefixexp '[' exp ']' | prefixexp '.' Name
G4.4  functioncall ::= prefixexp args | prefixexp ':' Name args
G4.5  args         ::= '(' [ explist ] ')' | tableconstructor | LiteralString
G4.6  explist      ::= exp { ',' exp }
```

- **G4.7 Primary expressions.** A `prefixexp` must start with a `Name` or `(`. Anything else
  where an expression is expected is `unexpected symbol near 'x'`. **(impl)** So `"s":upper()` and
  `{}.x` are syntax errors; `("s"):upper()` is fine.
- **G4.8 Suffixes.** After a primary expression, any number of suffixes may follow, applied left
  to right: `.Name`, `[exp]`, `:Name args`, and `args`. `a.b[c](d):e"f"{g}` is one expression.
- **G4.9 Method calls.** `obj:m(args)` calls `obj.m` with `obj` as an extra first argument, and
  evaluates `obj` once. After `:Name` the arguments are required: `obj:m` alone is
  `function arguments expected`. **(impl)**
- **G4.10 String and table arguments.** `f"str"`, `f[[str]]` and `f{...}` are calls with exactly
  one argument.
- **G4.11 Parentheses.** `(exp)` is a separate node, not just grouping: it truncates a call or
  `...` to exactly one value (`(f())` is one value), and the result is never a variable, so it
  cannot be assigned to.
- **G4.12 `...`** is only allowed inside a vararg function (one whose parameter list ends with
  `...`, or the main chunk). Elsewhere it is `cannot use '...' outside a vararg function`.
  **(impl)**

## G5. Functions

```
G5.1  functiondef ::= 'function' funcbody
G5.2  funcbody    ::= '(' [ parlist ] ')' block 'end'
G5.3  parlist     ::= namelist [ ',' '...' ] | '...'
```

- **G5.4** A parameter list that is not a name or `...` is `<name> or '...' expected`. **(impl)**
- **G5.5** Parameters are locals of the function body.
- **G5.6** A nested function can use the locals of the functions around it; those become its
  *upvalues*. The parser decides, for every name, whether it is a local, an upvalue or a global.

## G6. Table constructors

```
G6.1  tableconstructor ::= '{' [ fieldlist ] '}'
G6.2  fieldlist        ::= field { fieldsep field } [ fieldsep ]
G6.3  field            ::= '[' exp ']' '=' exp | Name '=' exp | exp
G6.4  fieldsep         ::= ',' | ';'
```

- **G6.5** `Name = exp` is the same as `["Name"] = exp`. To tell `{x = 1}` from `{x}`, the parser
  must look one token past the name: a field is `Name = exp` only when the token after the
  `Name` is `=`.
- **G6.6** Positional fields (`exp` alone) get the keys 1, 2, 3, ... in order. If the **last**
  field is a call or `...`, all its values are added; any other call or `...` gives one value.
- **G6.7** A trailing separator is allowed: `{1, 2, 3,}`.

## G7. Operators and precedence

```
G7.1  binop ::= '+' | '-' | '*' | '/' | '//' | '^' | '%'
              | '&' | '~' | '|' | '>>' | '<<' | '..'
              | '<' | '<=' | '>' | '>=' | '==' | '~='
              | 'and' | 'or'
G7.2  unop  ::= '-' | 'not' | '#' | '~'
```

- **G7.3 Precedence**, from lowest to highest:

  | Level | Operators                     | Associativity |
  | ----- | ----------------------------- | ------------- |
  | 1     | `or`                          | left          |
  | 2     | `and`                         | left          |
  | 3     | `<` `>` `<=` `>=` `~=` `==`   | left          |
  | 4     | `\|`                          | left          |
  | 5     | `~` (xor)                     | left          |
  | 6     | `&`                           | left          |
  | 7     | `<<` `>>`                     | left          |
  | 8     | `..`                          | **right**     |
  | 9     | `+` `-`                       | left          |
  | 10    | `*` `/` `//` `%`              | left          |
  | 11    | unary `not` `#` `-` `~`       | (prefix)      |
  | 12    | `^`                           | **right**     |

- **G7.4** `^` binds tighter than a unary operator on its **left**: `-x^2` is `-(x^2)`. On its
  right, a unary operator is allowed: `2^-3` is `2^(-3)`, and `2^-3^2` is `2^(-(3^2))`.
- **G7.5** `..` and `^` are right-associative: `a..b..c` is `a..(b..c)`, `a^b^c` is `a^(b^c)`.
  All other binary operators are left-associative: `a-b-c` is `(a-b)-c`.
- **G7.6** Comparisons chain syntactically (`a < b < c` is `(a < b) < c`), which fails at run
  time, not in the parser.
- **G7.7** Unary operators nest: `not not x`, `- -x`, `~~x`, `#-x`. (`--x` is a comment, not two
  minus signs.)
- **G7.8 How Lua implements it (impl):** each binary operator has a *left* and *right* priority,
  and unary operators have priority 12:

  | Operator        | Left | Right |
  | --------------- | ---- | ----- |
  | `or`            | 1    | 1     |
  | `and`           | 2    | 2     |
  | comparisons     | 3    | 3     |
  | `\|`            | 4    | 4     |
  | `~`             | 5    | 5     |
  | `&`             | 6    | 6     |
  | `<<` `>>`       | 7    | 7     |
  | `..`            | 9    | 8     |
  | `+` `-`         | 10   | 10    |
  | `*` `/` `//` `%`| 11   | 11    |
  | `^`             | 14   | 13    |

  To parse an expression with a limit *L*: read a unary operator and its operand (with limit
  12), or a simple expression; then, while the next token is a binary operator whose *left*
  priority is greater than *L*, read its right operand with the operator's *right* priority as
  the new limit. A right priority lower than the left one is what makes `..` and `^`
  right-associative.

## G8. `goto`, labels and `break`

- **G8.1** A label is visible in the whole block where it is defined, including nested blocks,
  but not inside nested functions.
- **G8.2** `goto Name` jumps to a visible label. If none is visible:
  `no visible label 'x' for <goto> at line N`. **(impl)**
- **G8.3** Two labels with the same name visible in the same place:
  `label 'x' already defined on line N`. **(impl)**
- **G8.4** A `goto` may not jump **into the scope of a local**:
  `<goto x> at line N jumps into the scope of local 'y'`. **(impl)**
- **G8.5** A label that is the last statement of its block (only `;`, other labels or nothing
  after it) counts as being outside the scope of the block's locals, so `goto continue` with
  `::continue::` at the end of a loop body is fine even if the body declares locals. **(impl)**
- **G8.6** `break` ends the innermost `while`, `repeat` or `for` loop. Outside a loop:
  `break outside loop at line N`. **(impl)**
- **G8.7** `break` and `goto` may appear anywhere in a block, not only as the last statement.

## G9. Syntax errors

- **G9.1** Errors use the lexer's format (L10.1): `<chunk>:<line>: <message> near <token>`, with
  the line of the token where the error was noticed.
- **G9.2** The `near` part (L10.2) shows the **current** token: names and numerals as written,
  keywords and symbols in quotes (`'end'`, `'=='`), and `<eof>` at the end. A string token shows
  its delimiters, as `'"abc"'`. **(impl)**
- **G9.3** The messages are:

  | Message                                   | When                                             |
  | ----------------------------------------- | ------------------------------------------------ |
  | `'X' expected`                            | a required token is missing: `'=' expected near 'y'`, `'then' expected near <eof>` |
  | `<name> expected`                         | a name is required: `local 1` gives `<name> expected near '1'` |
  | `'X' expected (to close 'Y' at line N)`   | a closing word is missing and the opening word is on an **earlier** line: `'end' expected (to close 'function' at line 3) near <eof>`. On the same line, the short form `'end' expected near ...` is used. |
  | `<eof> expected`                          | extra input after the chunk, such as a stray `end` (no quotes around `<eof>`) |
  | `unexpected symbol`                       | an expression was expected (G4.7)                 |
  | `syntax error`                            | a statement that is neither a call nor an assignment (G3.8, G3.9) |
  | `function arguments expected`             | `obj:m` without arguments (G4.9)                  |
  | `'=' or 'in' expected`                    | a broken `for` (G3.10)                            |
  | `<name> or '...' expected`                | a broken parameter list (G5.4)                    |
  | `cannot use '...' outside a vararg function` | G4.12                                          |
  | `unknown attribute 'x'`, `multiple to-be-closed variables in local list`, `attempt to assign to const variable 'x'` | G3.15 |
  | the `goto` and `break` messages           | G8                                                |

- **G9.4** Like the lexer, the parser stops at the first error.

## G10. Limits (impl)

The reference implementation refuses programs beyond these sizes. They are far from real code;
we only need to stay safe (no crash) near them, not match them exactly.

- At most 200 local variables active in one function: `too many local variables (limit is 200)
  in function at line N` (or `in main function`).
- At most 255 upvalues in one function: `too many upvalues (limit is 255) ...`.
- Nesting depth (of blocks, expressions and calls) is limited, about 200 levels, to protect the C
  stack. Our parser is recursive too, so it needs a similar depth limit with a clear error rather
  than a stack overflow.
