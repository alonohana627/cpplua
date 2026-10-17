# Coroutines, hooks and the debug library

The rules for roadmap stage 10: coroutines (Lua 5.4 Reference Manual §2.6 and §6.2), debug
hooks and the whole debug library (§4.7 and §6.10). Behavior, results and error messages are
the reference implementation's (Lua 5.4.9's `ldo.c`, `lvm.c`'s `luaV_finishOp`, `lcorolib.c`,
`ldebug.c`, `ldblib.c`). The manual is not restated here: these rules name how lua2 does what
Lua does, and what it decides where Lua leaves a choice open.

The my_lua programs in `tests/fixtures/my_lua/coroutines/` check coroutines against the real
Lua's output; the official test suite's `coroutine.lua`, `db.lua` and `locals.lua` check the
rest ([conformance.md](conformance.md)). How the code works is in
[../final_docs/coroutines.md](../final_docs/coroutines.md) and
[../final_docs/debug.md](../final_docs/debug.md).

---

## C1. Yields are return values

- **C1.1** A coroutine is a thread (`type(co) == "thread"`): its own stack and frames, created
  by `coroutine.create`, collected when nothing refers to it.
- **C1.2** A yield is a status, like an error (R1.1): `Status::Yield` goes back up through every
  C++ frame between the yield and the `resume` that started them, and each returns at once,
  leaving its frames and stack as they are. No `longjmp`, no C++ exceptions, no second machine
  stack: a coroutine costs its Lua stack and frames only.
- **C1.3** `resume` continues a suspended coroutine where it stopped: in the Lua frame on top,
  or in the C++ function that yielded (its continuation, C3), with the values passed to
  `resume` as the yield's results.

## C2. Where a coroutine can yield

- **C2.1** Lua code can yield anywhere, metamethods included: `__index`, `__newindex`, the
  arithmetic, bitwise, comparison, concatenation, length, `__close` and `__call` metamethods
  may all yield when the VM calls them (as `luaT_callTM` decides: when the frame below is Lua
  code).
- **C2.2** A C++ function can only be yielded across through a call with a continuation (C3).
  Every other call from C++ (`__gc` finalizers, `__tostring` from `tostring`, `table.sort`'s
  comparison, `string.gsub`'s function, hooks, ...) is not yieldable: a yield inside is
  `attempt to yield across a C-call boundary`. Outside any coroutine it is `attempt to yield
  from outside a coroutine`. Each thread counts its non-yieldable calls (the main thread has
  one), as Lua's `nCcalls` does.
- **C2.3** `coroutine.isyieldable` and `coroutine.running` answer from that count and the
  running thread.

## C3. Continuations

- **C3.1** A C++ function that calls Lua and may be yielded across passes a continuation:
  `call_k` (`lua_callk`) or, for a protected call, `protected_call_k` (`lua_pcallk`). After the
  resume, the continuation runs in place of the rest of the function, with the call's results.
  In the standard library: `pcall`, `xpcall`, `dofile` and `pairs` (calling `__pairs`).
- **C3.2** A yieldable protected call that fails after a resume is caught where it was made:
  the frames above it are dropped, its to-be-closed variables closed (they may yield too), and
  its continuation runs with `Status::Error` (Lua's `precover`).

## C4. An instruction stopped by a yield finishes after the resume

- **C4.1** A yield inside a metamethod stops the instruction that called it. After the resume,
  the instruction is finished with the metamethod's result (Lua's `luaV_finishOp`): the result
  goes to the instruction's register, a comparison jumps or not (`a <= b` done as
  `not (b < a)` is negated), a concatenation goes on with the rest of its operands, a `RETURN`
  that was closing variables goes on returning.

## C5. The coroutine library

- **C5.1** `coroutine.create`, `resume`, `running`, `status`, `wrap`, `yield`, `isyieldable` and
  `close` are `lcorolib.c`'s, with its messages: `cannot resume non-suspended coroutine`,
  `cannot resume dead coroutine`, `cannot close a running coroutine`, `cannot close a normal
  coroutine`, `too many arguments to resume`, `too many results to resume`.
- **C5.2** Nested resumes count as C++ calls: more than 200 at once is `C stack overflow`
  (`LUAI_MAXCCALLS`).
- **C5.3** A coroutine that dies of an error keeps its frames (for `debug.traceback(co)`) until
  `coroutine.close` empties it. `close` runs its pending `__close` metamethods and returns `true`,
  or `false` and the error.
- **C5.4** An error through `wrap`'s function is raised again in the caller, a string with the
  caller's position in front, after the coroutine is closed.

## H1. Hooks

- **H1.1** Each thread has its own hook (`debug.sethook`): call (with `"tail call"`), return,
  line and count events, as Lua sends them. A line hook runs when the line changes, on a jump
  back, and on the first instruction of a function; a count hook every `count` instructions.
- **H1.2** No hook runs while a hook is running, and a hook cannot yield. A hook function's
  frame is named `hook '?'` in tracebacks.
- **H1.3** The hook functions are kept in the registry's `_HOOKKEY` table, weak in its keys
  (the threads), as `ldblib.c` keeps them.

## H2. Hooks cost nothing when no thread has one

- **H2.1** The VM dispatches through a table of instruction handlers. While some thread has a
  hook, every entry is a handler that checks the hooks first; when none has, the table is the
  plain handlers again. With the `switch` dispatch, a flag is checked per instruction instead.

## H3. The debug library

- **H3.1** All of `ldblib.c`: `debug`, `gethook`, `getinfo`, `getlocal`, `getmetatable`,
  `getregistry`, `getupvalue`, `getuservalue`, `sethook`, `setlocal`, `setmetatable`,
  `setupvalue`, `setuservalue`, `traceback`, `upvalueid`, `upvaluejoin`, `setcstacklimit`, with
  the optional thread argument where Lua has it.
- **H3.2** `getinfo` fills the fields of Lua's `lua_getinfo` options (`S`, `l`, `u`, `n`, `t`,
  `r`, `L`, `f`), with the same names for functions (`global 'f'`, `method 'm'`, `metamethod
  'index'`, `for iterator`, `hook`, ...). `debug.setcstacklimit` changes nothing and returns
  200, as 5.4.9's does.
- **H3.3** The registry (`debug.getregistry()`) is a table holding the main thread at `[1]`,
  the global table at `[2]`, `_LOADED` (which `package.loaded` is), `_PRELOAD` and, once a hook
  is set, `_HOOKKEY`.

## H4. Interrupts

- **H4.1** `request_interrupt()` (the stand-alone program's Ctrl-C handler) stops the running
  Lua code at its next instruction with the error `interrupted!`, as `lua.c`'s `laction` does
  with a hook. It is safe to call from a signal handler.
