# Errors

How lua2 raises and catches errors (roadmap stage 8). This page describes the code as it is. The
rules are in [../spec/errors.md](../spec/errors.md), how the stage was planned in
[../plan/errors.md](../plan/errors.md), and the background in
[../learn/tables_metatables_errors.md](../learn/tables_metatables_errors.md).

**Status:** stage 8 is done (version 0.0.8): `error`, `assert`, `pcall`, `xpcall`, error objects of
every type, the C-call limit, and stack tracebacks that match the real Lua's line for line.

## Where it lives

| File                                                | Contents                                     |
| --------------------------------------------------- | -------------------------------------------- |
| `lib/internal/vm/execute.hpp`, `lib/vm/execute.cpp` | `call`, `protected_call`, `runtime_error`, closing |
| `lib/internal/vm/state.hpp`                         | `State::error_value`, `CallInfo`'s flags      |
| `lib/internal/std/auxlib.hpp`, `lib/std/auxlib.cpp` | `where`, argument errors, `traceback`         |
| `lib/std/base.cpp`                                  | `error`, `assert`, `pcall`, `xpcall`, `debug.traceback` |
| `lib/vm/run.cpp`                                    | running a chunk the way `lua.c` does          |
| `tests/error_test.cpp`, `tests/fixtures/my_lua/errors/` | tests                            |

## The decision: errors are return values

Lua unwinds an error with `longjmp` (or a C++ exception when built as C++). lua2 does neither: a
failing instruction returns `Status::Error`, a C++ library function returns `c_function_error`,
and every function above passes it on, until a protected call takes it. The error object itself
is in `State::error_value` (a collector root), as Lua keeps it on the stack top.

This follows the project's choice for the lexer and parser (`std::expected`, no exceptions), and
it costs nothing where it matters: VM handlers already return a status, so a check is a compare
on a path that was already there, and a `pcall` is a plain call. The benchmark `pcall` runs in
0.20x of the real Lua's time, which pays for `setjmp` on every protected call.

The one subtlety: **nothing is unwound while the error travels up**. The frames (`State::ci`),
the stack and the count of C++ calls stay as they were where the error happened, exactly as in
Lua until its `longjmp`. So the protected call that catches the error can run the message
handler *there* (xpcall's handler sees the whole stack, R2.3), and only then cut back.

## Raising

- `runtime_error` (`luaG_runerror`): the message with the running Lua function's position
  (`chunk:line: `), from its saved pc. VM handlers save the pc first (`raise`, `protect`).
- Error messages name values as Lua does (`ldebug.c`): `type_error`, `call_error` (named from the
  calling instruction, including `metamethod 'add'` for metamethods), `index_error` (a register or
  an upvalue), `order_error`, all with `__name`.
- `error(v, level)` puts `where(level)` in front of strings (`luaL_where`: the `level`-th frame up,
  if it is a Lua function with a line); `assert` does the same with its message.
- Library functions raise argument errors through `arg_error` (`luaL_argerror`): the function's
  name as its caller called it (`function_name`, from the calling instruction), or its global name
  (`global_function_name`, searching the loaded modules), and `self` is not counted for methods.

## Catching

`protected_call(thread, func, wanted, handler)` (`luaD_pcall`):

1. `call()`; if it succeeds, nothing more.
2. On an error, the message handler (if any) runs where the error happened: pushed above the
   erroring frame, its result replacing the error object (`luaG_errormsg`). If the handler fails,
   it is called again for the new error, deeper, as in Lua, until the C-call limit turns it into
   `error in error handling` (R2.4).
3. Back to the protected call's frame and C-call count; `close_protected` closes the upvalues and
   to-be-closed variables of the dropped frames with the error object (an error in a `__close`
   replaces it, and the closing goes on, M6.3).
4. The error object goes where the function was, and the stack shrinks back if an overflow had
   grown it (`luaD_shrinkstack`).

`pcall` puts `true` below the function and returns everything from there, or `false` and the
error; `xpcall` keeps its handler in its own frame.

## Limits (R3)

- The Lua stack: `stack overflow` past a million slots, with 200 more to handle the error
  (`State::grow_stack`, unchanged from stage 5, now catchable).
- C++ calls (`call()`, Lua's `ccall`): the 200th nested one is `C stack overflow`, with the
  position of the function making the call; from 220 on, `error in error handling` without a
  position (`luaE_checkcstack`). Calls between Lua functions do not count: they stay inside the
  VM.

## Tracebacks (R4)

`traceback` (`luaL_traceback`) walks the frames from a level down to the bottom (`frame_at`, Lua's
`lua_getstack`), showing the first 10 and last 11 of a deep stack. Each line has the position
(`[C]` for C++ functions) and the function's description (`pushfuncname`): its global name if the
loaded modules have it, else how its caller called it, else `main chunk`, `function
<chunk:line>` or `?`. A function entered by a tail call has lost its caller, so it has no name and
gets a `(...tail calls...)` line (`CallInfo::tail_call`, Lua's `CIST_TAIL`).

`lua2 file.lua` runs the chunk the way `lua.c` does: from a C++ function (`pmain` in
`lib/standalone.cpp` since stage 11, `run_chunk` for `Interpreter::run`; that is why tracebacks
end with `[C]: in ?`), in a protected call whose message handler
(`message_handler`, `msghandler`) adds the traceback. An error object that is not a string shows
as its `__tostring` result, or `(error object is a X value)` (R2.5). The tests compare lua2's
tracebacks with the real Lua's (`tests/error_test.cpp`).
