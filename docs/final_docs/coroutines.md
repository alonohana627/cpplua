# Coroutines

**Status:** stage 10 is done (version 0.0.11, with stage 11): Lua 5.4's coroutines, yielding
from anywhere Lua can (metamethods, `pcall`, `<close>`), with the `coroutine` library. The rules
are in [../spec/coroutines.md](../spec/coroutines.md), the plan in
[../plan/coroutines.md](../plan/coroutines.md), and the background in
[../learn/coroutines.md](../learn/coroutines.md). Hooks and the debug library, built in the
same stage, are in [debug.md](debug.md).

## What exists

| Function | Port of |
| -------- | ------- |
| `coroutine.create`, `resume`, `running`, `status`, `wrap`, `yield`, `isyieldable`, `close` | `lcorolib.c` |
| `resume`, `yield_values`, `close_thread` (`lib/internal/vm/execute.hpp`) | `ldo.c`'s `lua_resume`, `lua_yieldk`, `lua_closethread` |
| `call_k`, `protected_call_k` | `lua_callk`, `lua_pcallk` |

Nothing differs from the real Lua: the my_lua programs in `tests/fixtures/my_lua/coroutines/`
give the reference Lua's output, and the suite's `coroutine.lua` passes.

## Where it lives

| File | Contents |
| ---- | -------- |
| `lib/internal/vm/state.hpp` | `Status::Yield`, `Continuation`, `ThreadStatus`; a thread's `status`, `yielded` and `non_yieldable`; the frame fields for yields (`negate_compare`, `yieldable_pcall`, `pcall_error`, `return_count`, `continuation`, `context`, `pcall_func`, `pcall_handler`) |
| `lib/vm/execute.cpp` | the call paths (`call_body`, `call_k`, `protected_call_k`), and the section "Coroutines": `finish_instruction`, `finish_c_call`, `unroll`, `recover_errors`, `resume`, `yield_values`, `close_thread` |
| `lib/vm/handlers.inc` | every instruction that can call a metamethod passes a yield up |
| `lib/std/coroutine.cpp` | the library; `std/coroutine.lua` its reference |
| `lib/std/base.cpp` | `pcall`, `xpcall`, `dofile`, `pairs` with continuations (`finish_pcall`, ...) |
| `lib/gc/heap.cpp`, `collector.cpp` | `new_thread`, the running thread, threads traversed and freed by the collector |

## How it works

**A coroutine is a `State`.** `coroutine.create` makes a new thread in the heap (its own stack
and frames, the shared heap and globals) with the function in its first slot. The value is a
`Tag::Thread`. The heap knows which thread is running: finalizers run in it, and
`coroutine.running` answers from it.

**Yielding.** `coroutine.yield` calls `yield_values(thread, n)`, which checks the thread may
yield (C2: it is a coroutine and `non_yieldable` is 0), records how many values it passes and
returns `c_function_yield`. The C++ call path turns that into `Status::Yield`, and from there it
travels like an error: each `execute()`, metamethod call and C++ function in between returns it
at once, leaving its frame where it was. `resume` receives it with the yielded values on the
coroutine's top, and the coroutine's status becomes `Yield`.

**Which calls can be yielded across.** `non_yieldable` counts the calls a yield cannot cross
(the main thread starts with 1). `call()` (the VM calling a metamethod) is yieldable when the
frame below is Lua code, as `luaT_callTM` decides; `call_no_yield()`, used by C++ functions
calling Lua without a continuation, adds 1 for the call. `call_k` and `protected_call_k` keep
it yieldable and store the continuation in the C++ function's frame.

**Resuming.** `resume(co, from, nargs, results)` checks the coroutine can be resumed (C5.1)
and the depth of nested resumes (C5.2), then runs `resume_body`: a new coroutine calls its
function; a suspended one gets the resume's values as the yield's results and runs `unroll`,
which finishes every frame from the top down:

- a C++ frame runs its **continuation** (`finish_c_call`) with what the call it made returned,
  or, if it was in a yieldable `pcall` closing variables after an error, goes on closing them;
- a Lua frame first **finishes the instruction the yield interrupted** (`finish_instruction`,
  Lua's `luaV_finishOp`, C4): a metamethod's result goes to the instruction's register, a
  comparison's (negated when `a <= b` ran as `not (b < a)`) decides the jump, a concatenation
  goes on with its remaining operands, a `RETURN` closing variables returns. Then `execute()`
  goes on from the next instruction.

**Errors after a resume** (`recover_errors`, Lua's `precover`): an error with a yieldable
`pcall` below it in the coroutine is caught there: its message handler runs where the error
happened, the frames above are dropped and their variables closed, and the `pcall`'s
continuation runs with `Status::Error`. Without one, the coroutine is dead and the error goes
to the resumer, left on the coroutine's top (and its frames kept for `debug.traceback(co)`).

**Closing** (`close_thread`, `coroutine.close` and `wrap`'s error path): the coroutine's
pending `__close` metamethods run with its error (or `nil`), its upvalues are closed, and its
stack and frames are emptied; it is dead.

## Tests

- `tests/fixtures/my_lua/coroutines/`: 28 programs (generators, yields inside metamethods,
  `pcall`, `<close>`, `wrap`, errors and their messages), each with the reference Lua's output.
- The official suite's `coroutine.lua`, `cstack.lua`, `locals.lua` and `db.lua`
  ([conformance.md](conformance.md)).
- The GC stress run of the my_lua programs collects at every allocation, coroutines included.
