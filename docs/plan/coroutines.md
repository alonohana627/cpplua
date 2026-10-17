# Coroutines plan

Roadmap stage 10. Rule ids such as `C1.2` point to [../spec/coroutines.md](../spec/coroutines.md).
How the finished code works is in [../final_docs/coroutines.md](../final_docs/coroutines.md) and
[../final_docs/debug.md](../final_docs/debug.md); the background is in
[../learn/coroutines.md](../learn/coroutines.md).

## Status: done (0.0.11, with stage 11)

## Decisions

- **A yield is a status, like an error** (C1.2). Stage 8 made errors return values instead of
  `longjmp`s; a yield takes the same road: `Status::Yield` returns through every C++ frame up
  to the `resume`, which leaves the coroutine's frames as they are. Lua itself does it with a
  `longjmp` to the resume; return values make every path that can yield visible in the code,
  and need no second machine stack per coroutine (as stackful C++ coroutines or fibers would).
- **Ports of Lua's resume machinery.** `resume`, `unroll`, `finishCcall`, `luaV_finishOp`
  (C4), `precover` and `lua_closethread` are followed step by step, with Lua's frame flags
  (`CIST_LEQ`, `CIST_YPCALL`, `CIST_RECST`) as named `CallInfo` fields.
- **Continuations as in Lua's C API** (C3): `call_k` and `protected_call_k` take a function and
  a context, for the library functions that call Lua and must survive a yield (`pcall`,
  `xpcall`, `dofile`, `pairs`).
- **The debug library comes with it, whole.** Coroutines need `debug.traceback(co)` and the
  suite's `coroutine.lua` uses hooks, so stage 10 ports all of `ldblib.c` and Lua's hooks
  (H1, H3), not only `traceback`.
- **Hooks cost nothing when unused** (H2): the VM's dispatch table is swapped for checking
  handlers only while a thread has a hook. Measured against stage 9: no slower on any benchmark.
- **The registry is a real table** (H3.3), as Lua's: the main thread, the globals, `_LOADED`
  and `_HOOKKEY` live there, so `debug.getregistry()` is Lua's.

## Parts

1. **Threads as values**: `Tag::Thread`, a thread per coroutine in the collector, the running
   thread in the heap (finalizers run in it).
2. **`Status::Yield`** through the VM, the call paths and the library; yieldability counted per
   thread (C2).
3. **`resume`**: starting, finishing interrupted C++ frames (continuations) and Lua frames
   (C4), errors after a resume caught by yieldable `pcall`s (C3.2), `close_thread`.
4. **The coroutine library** (`lib/std/coroutine.cpp`, C5) and its reference `std/coroutine.lua`.
5. **Hooks** in the VM and the C++ call path (H1, H2), and the interrupt (H4).
6. **The debug library** (`lib/std/debug.cpp`, H3) and `std/debug.lua`.
7. **Golden programs** in `tests/fixtures/my_lua/coroutines/`; the suite's `coroutine.lua`,
   `db.lua`, `locals.lua` and `cstack.lua` ([conformance.md](conformance.md)).
