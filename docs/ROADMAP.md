# Roadmap

Each stage ends with green tests before the next starts.

1. **Skeleton + tests** (done): CMake + Ninja presets, clang++ with libc++, C++26, Catch2 v3 via FetchContent, public headers in `include/lua2/`, the embeddable `lua2::lua2` library in `lib/`, the `lua2` executable in `src/`, CTest.
2. **Lexer** (done, 0.0.2; see [final_docs/lexer.md](final_docs/lexer.md)): source text to tokens (names, keywords, numbers incl. hex floats, long strings and comments, escapes), with line numbers for errors.
3. **Parser + AST** (done, 0.0.3; see [final_docs/parser.md](final_docs/parser.md), plan: [plan/parser.md](plan/parser.md)): recursive descent with Lua's operator precedence; statements, blocks, functions; syntax errors that match Lua's wording.
   - **AST viewer**: `lua2 --ast file.lua` prints the tree as indented text, and `lua2 --ast=dot file.lua` writes Graphviz DOT to render as a graph (`dot -Tsvg`). Built with the parser so every parser test can show its tree.
4. **Values** (done, 0.0.4; see [final_docs/values.md](final_docs/values.md), plan: [plan/values.md](plan/values.md)): nil, boolean, integer vs float (5.4 integer subtype), strings; arithmetic and comparison rules, string/number coercion.
5. **Bytecode VM + garbage collector** (done, 0.0.5; see [final_docs/vm.md](final_docs/vm.md); one stage, user's decision 2026-10-17; plan: [plan/vm.md](plan/vm.md), rules: [spec/execution.md](spec/execution.md)): no tree-walking interpreter. A compiler turns the AST into Lua-style register bytecode (like `lcode.c`), and a VM runs it (like `lvm.c`): locals and scopes, control flow, `goto`, varargs, multiple returns, calls of Lua functions, closures and upvalues (moved here from the old stage 7, 2026-10-17, since calls and the collector need them), compile-time `<const>` constants. The garbage collector takes the place of the string `Heap` and owns every object (strings, function prototypes, closures, upvalues), with both of Lua 5.4's modes: incremental and generational. The VM's stack stores values as split tags and payloads (stage 4's decision). Ahead of stages 6 and 9, at the user's request: globals live in a minimal globals table (string keys only), the VM calls C++ functions, and the standard library has `print`.
6. **Tables**: array part + hash part, `#` border, `next`/`pairs`; collected by the GC. Replaces stage 5's string-keys-only globals table.
7. **Metatables**: `__index`, `__newindex`, arithmetic/comparison metamethods, `__call`, `__tostring`, `__gc`, `__close` and to-be-closed variables (`<close>`), `__mode` (weak tables) and finalizers in the GC.
8. **Errors**: `error`, `pcall`/`xpcall`, tracebacks with line info.
9. **Standard library**: base, string (incl. patterns and `string.format`), table, math, utf8, os/io subsets.
10. **Coroutines**: `coroutine.*`, yield across the VM.
11. **Conformance**: run the official Lua 5.4 test suite (`lua-5.4.x-tests`) and work through failures.
12. **Multi-error mode** (optional, for editors and linters): the lexer and parser recover after an error and report every error in a chunk. The default stays one error at a time, like Lua.
13. **JIT compiler** (optional, future): compile hot bytecode to machine code at run time, specializing on observed types and falling back to the VM (deoptimizing) whenever an assumption breaks: metamethods, integer/float rules, coroutines, the `debug` library and error messages must behave exactly as in the VM. Built after conformance (stage 11) and held to the same official test suite. Binary chunks (`string.dump`, `luac` output) stay incompatible with PUC Lua whether or not there is a JIT; source code is always compatible.

## Decisions

- **Errors in the lexer and parser** are returned with `std::expected<..., SyntaxError>`, not thrown, so every error path is visible in the code. Lexing and parsing stop at the first error. How the VM handles `error()`/`pcall` is decided in stage 8.
- **The AST is an arena**: nodes live in `std::vector`s owned by one `Ast` and refer to children by typed 32-bit indices; each node is a `std::variant` of plain structs. Chosen for speed and cache locality (see `learn/ast_representation.md`).
- **Token source text**: every `Token` stores its offset and length in the source (two `std::uint32_t`), so errors can quote it exactly as written.
- **Name resolution happens while parsing**, like Lua's `lparser.c`: one pass, and compile-time errors in Lua's exact order. Scope bookkeeping lives in its own class that the parser calls.
- **Parser lookahead** is a one-token buffer in the parser; the lexer stays a stream. Fastest when measured (lexing everything into a vector first was ~2.2x slower).
