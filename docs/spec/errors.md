# Lua 5.4 errors

The rules for raising and catching errors (roadmap stage 8): `error`, `assert`, `pcall`,
`xpcall`, the limits on nesting, and stack tracebacks. It is based on the Lua 5.4 Reference
Manual §2.3 (error handling), §4.4 (C functions and errors, for the limits) and §6.1 (`error`,
`assert`, `pcall`, `xpcall`) and §6.10 (`debug.traceback`), restated in our own words. Where the
manual is silent, the reference implementation (`ldo.c`, `ldebug.c`, `lbaselib.c`, `lauxlib.c`,
`lua.c` in Lua 5.4.9) is the tie-breaker, and those rules are marked **(impl)**.

How runtime errors are worded, and where their position comes from, is in
[execution.md](execution.md) (E8).

Every rule has an id like `R2.1`. Tests should name the rule they check, for example
`TEST_CASE("R2.1 pcall returns false and the error", "[error]")`.

---

## R1. Raising errors

- **R1.1** Any value can be an error object: a string, a table, `nil`, ... Runtime errors raised
  by operations are strings with the position of E8.1.
- **R1.2** `error(v [, level])` raises `v`. When `v` is a string and `level` is positive, the
  position of the function at that level is put in front, as `chunk:line: `: level 1 (the
  default) is the function that called `error`, level 2 its caller, and so on. Level 0, a level
  with no Lua function, and values that are not strings add nothing.
- **R1.3** `assert(v [, message, ...])` returns all its arguments when `v` is truthy. Otherwise
  it raises `message` like `error(message)` would, or the string `assertion failed!` when there
  is no message; a missing `v` is `bad argument #1 to 'assert' (value expected)`.
- **R1.4** Errors in arguments of library functions name the argument and the function as the
  calling code names it: `bad argument #1 to 'setmetatable' (table expected, got nil)`; a method
  call does not count `self`. **(impl: `luaL_argerror`)**

## R2. Catching errors

- **R2.1** `pcall(f, ...)` calls `f` with the other arguments in protected mode. Without errors it
  returns `true` and all of `f`'s results; with an error anywhere inside, it returns `false` and
  the error object. `pcall` of a value that cannot be called catches that error too.
- **R2.2** An error unwinds every call between it and the nearest `pcall` or `xpcall`. On the way,
  open upvalues are closed (E7.3) and to-be-closed variables are closed with the error object
  (M6.3), innermost first; the stack shrinks back if it had grown.
- **R2.3** `xpcall(f, h, ...)` is like `pcall`, but on an error it calls the message handler `h`
  with the error object *before* unwinding, so the handler sees the stack as it was (to build a
  traceback); `xpcall` then returns `false` and the handler's first result.
- **R2.4** If the message handler itself raises an error, the handler is called again with the
  new error. **(impl: from inside the failed call, so a handler that always fails goes deeper
  until the C-call limit (R3.2) turns it into `error in error handling`)**
- **R2.5** An error not caught anywhere stops the program. `lua2 file.lua` prints `lua2: `, the
  message and a stack traceback (R4) on stderr, and exits with a failure status. An error object
  that is not a string or number shows as its `__tostring` result (without a traceback), or as
  `(error object is a X value)`. **(impl: `msghandler` in `lua.c`)**

## R3. Limits

- **R3.1** Lua functions can nest until the stack holds about a million values; then `stack
  overflow` (E6.7), which `pcall` can catch.
- **R3.2** Calls that go through C++ (metamethods, `pcall`, finalizers, message handlers, every
  call into the VM from C++) nest at most 200 deep: the 200th is the error `C stack overflow`
  (with the position of the calling function), and errors while handling that one, from 220
  levels on, are `error in error handling`. **(impl: `LUAI_MAXCCALLS` and `luaE_checkcstack`)**

## R4. Tracebacks

- **R4.1** `debug.traceback([message [, level]])` returns `message` (if given), a newline, and
  `stack traceback:` followed by one line per active function, from level `level` (default 1, the
  function calling `traceback`) to the bottom: a tab, the position (`chunk:line:`, or the chunk
  alone for a C++ function, `[C]:`), ` in `, and the function's description. A message that is
  not a string or `nil` is returned as it is, without a traceback.
- **R4.2** A function is described as `function 'name'` when it is a global (found in the loaded
  modules, `_G` included), else by how it was called: `local 'f'`, `upvalue 'f'`, `method 'm'`,
  `field 'f'`, `for iterator 'for iterator'`, `metamethod 'index'`; else `main chunk`, `function
  <chunk:line>` for a Lua function, or `?`. A function entered by a tail call has no name, and is
  followed by the line `(...tail calls...)`. **(impl: `pushfuncname` and `getfuncname`)**
- **R4.3** With more than 22 levels, the traceback shows the first 10 and the last 11, with a
  line `...	(skipping N levels)` between them. **(impl: `LEVELS1` and `LEVELS2`)**
