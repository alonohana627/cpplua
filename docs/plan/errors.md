# Errors plan

Roadmap stage 8. Rule ids such as `R2.3` point to [../spec/errors.md](../spec/errors.md). How the
finished code works is in [../final_docs/errors.md](../final_docs/errors.md); the background is
in [../learn/tables_metatables_errors.md](../learn/tables_metatables_errors.md).

## Status: done (0.0.8)

Built in one go with stages 6 and 7, at the user's request (see [tables.md](tables.md)). This
completes the three stages and bumps the version to 0.0.8.

## Decisions

- **Errors are return values** (the decision the roadmap left to this stage): `Status::Error`
  from the VM, `c_function_error` from C++ functions, the error object in `State::error_value`.
  No `longjmp`, no C++ exceptions, as in the lexer and the parser. Measured, a `pcall` is about
  five times cheaper than the real Lua's (`bench/pcall.lua`).
- **Nothing unwinds on the way up.** The frames stay where the error happened until the
  protected call that catches it, so `xpcall`'s handler runs with the whole stack, as in Lua.
- **The count of C++ calls stays too**, and the protected call restores it, so the C-call limit
  and `error in error handling` come at the same depths as in Lua.
- **Tracebacks are `lauxlib.c`'s**, and `lua2 file.lua` runs a chunk the way `lua.c` does (from a
  C++ function, with a message handler), so its error output matches the real `lua`'s line for
  line. `debug.traceback` comes now rather than with the debug library (stage 9), since stage 8
  is "tracebacks".

## Parts

1. **Error objects**: `State::error_value`, `runtime_error`, every message of the VM through it.
2. **C++ functions that fail**: `c_function_error`, the `lauxlib` helpers (`arg_error` and the
   checks).
3. **`call` and `protected_call`**: the C-call limit, `__call`, the message handler, closing on
   errors.
4. **The library**: `error`, `assert`, `pcall`, `xpcall`, `debug.traceback`.
5. **Running chunks**: `run_text`, the traceback in `lua2`'s error output.
6. **Golden programs** `error_*.lua` and `err_*.lua`, and the rule tests
   (`tests/error_test.cpp`, which also compares tracebacks with the real Lua's).
