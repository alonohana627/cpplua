# Roadmap

Each stage ends with green tests before the next starts.

1. **Skeleton + tests** (done): CMake + Ninja presets, clang++ with libc++, C++26, Catch2 v3 via FetchContent, public headers in `include/lua2/`, the embeddable `lua2::lua2` library in `lib/`, the `lua2` executable in `src/`, CTest.
2. **Lexer**: source text to tokens (names, keywords, numbers incl. hex floats, long strings and comments, escapes), with line numbers for errors.
3. **Parser + AST**: recursive descent with Lua's operator precedence; statements, blocks, functions; syntax errors that match Lua's wording.
   - **AST viewer**: `lua2 --ast file.lua` prints the tree as indented text, and `lua2 --ast=dot file.lua` writes Graphviz DOT to render as a graph (`dot -Tsvg`). Built with the parser so every parser test can show its tree.
4. **Values**: nil, boolean, integer vs float (5.4 integer subtype), strings; arithmetic and comparison rules, string/number coercion.
5. **Interpreter**: tree-walking evaluator first (simple to test), locals and scopes, control flow, `goto`, varargs, multiple returns.
6. **Tables**: array part + hash part, `#` border, `next`/`pairs`.
7. **Closures + upvalues**: shared upvalues, `<const>` and `<close>`.
8. **Metatables**: `__index`, `__newindex`, arithmetic/comparison metamethods, `__call`, `__tostring`, `__gc`, `__close`.
9. **Garbage collector**: mark and sweep (later incremental/generational), weak tables, finalizers.
10. **Errors**: `error`, `pcall`/`xpcall`, tracebacks with line info.
11. **Standard library**: base, string (incl. patterns and `string.format`), table, math, utf8, os/io subsets.
12. **Coroutines**: `coroutine.*`, yield across the interpreter.
13. **Bytecode VM** (optional rewrite of stage 5 for speed), kept honest by the same tests.
14. **Conformance**: run the official Lua 5.4 test suite (`lua-5.4.x-tests`) and work through failures.
15. **Multi-error mode** (optional, for editors and linters): the lexer and parser recover after an error and report every error in a chunk. The default stays one error at a time, like Lua.

## Decisions

- **Errors in the lexer and parser** are returned with `std::expected<..., SyntaxError>`, not thrown, so every error path is visible in the code. Lexing and parsing stop at the first error. How the interpreter handles `error()`/`pcall` is decided in stage 10.
