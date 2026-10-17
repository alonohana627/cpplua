# Hooks and the debug library

**Status:** stage 10 is done (version 0.0.11): Lua 5.4's hooks and the whole `debug` library.
The rules are H1 to H4 in [../spec/coroutines.md](../spec/coroutines.md); coroutines, built
in the same stage, are in [coroutines.md](coroutines.md).

## What exists

| Function | Port of |
| -------- | ------- |
| `debug.debug`, `gethook`, `getinfo`, `getlocal`, `getmetatable`, `getregistry`, `getupvalue`, `getuservalue`, `sethook`, `setlocal`, `setmetatable`, `setupvalue`, `setuservalue`, `traceback`, `upvalueid`, `upvaluejoin`, `setcstacklimit` | `ldblib.c` |
| `set_hook` (`lib/internal/vm/execute.hpp`) | `lua_sethook` |
| the hooks in the VM and in C++ calls | `ldo.c`'s `luaD_hook`, `ldebug.c`'s `luaG_traceexec` |
| `request_interrupt` | `lua.c`'s `laction` |

## Where it lives

| File | Contents |
| ---- | -------- |
| `lib/std/debug.cpp` | the library; `std/debug.lua` its reference |
| `lib/vm/execute.cpp` | the section "Debug hooks": `run_hook`, `return_hook`, `trace_instruction`, `op_hooked`, the dispatch table, `set_hook`, the interrupt; C++ functions' call and return hooks in `call_c` |
| `lib/internal/vm/state.hpp` | a thread's hook fields (`hook_mask`, `base_hook_count`, `hook_count_left`, `allow_hook`, `old_pc`, `pending_call_hook`) and a frame's (`hooked`, `transfer`, ...) |
| `lib/std/auxlib.cpp` | function names for tracebacks and messages (`hook '?'`, `for iterator`, ...) |
| `lib/gc/heap.cpp` | the registry table |

## How it works

**Setting a hook.** `debug.sethook([thread,] f, mask, count)` stores `f` in the registry's
`_HOOKKEY` table under the thread (a table weak in its keys, so a dead coroutine's hook goes
with it) and calls `set_hook(thread, mask, count)` with Lua's event bits (`hook_call`,
`hook_return`, `hook_line`, `hook_count`).

**Hooks cost nothing when no thread has one** (H2). The tail-call VM dispatches through a table
of 128 instruction handlers. `set_hook` keeps a count of threads with hooks; when it goes from
0 to 1, every entry of the table becomes `op_hooked`, and when it drops back to 0, the plain
handlers return. So without hooks the VM runs exactly as before; with them, each instruction
first goes through `op_hooked`, which runs `trace_instruction` and then the real handler (from
the plain table, so the hooks see it once). The table is shared by every interpreter in the
program, read and written with relaxed atomics; a hook in one interpreter makes the others
check too (and find nothing to do). The `switch` dispatch checks an atomic flag instead.

**`trace_instruction`** (Lua's `luaG_traceexec`) does, for the running thread:

- a pending call hook first: set by `CALL` and `TAILCALL` into a Lua function, it runs at the
  callee's first instruction (after `VARARGPREP` for a vararg function, as in Lua), as `call`
  or `tail call`;
- the count hook, when `count` instructions have run;
- the line hook, when the line changes, on a jump backwards, or at a function's first
  instruction; `old_pc` remembers the last instruction it saw;
- return hooks are run by the `RETURN` instructions, with the values returned (`getinfo`'s
  `r` fields `ftransfer` and `ntransfer`).

C++ functions get their call and return hooks in `call_c`. A hook runs through `run_hook`:
not inside another (`allow_hook`), with the frame's registers protected, as a non-yieldable
call (H1.2); its frame is marked `hooked`, so tracebacks name it `hook '?'`.

**The library** follows `ldblib.c` function by function. `getinfo` reads a frame (or a
function) into Lua's `lua_Debug` fields; `getlocal` and `setlocal` find a frame's locals by the
prototype's local variable ranges at the frame's instruction, then varargs at negative indices
and temporaries (`(temporary)`, `(C temporary)`); `getupvalue` names C++ upvalues `""`;
`upvalueid` and `upvaluejoin` use the `Upvalue` objects themselves. `traceback` is the one of
`lauxlib.c` (`luaL_traceback`), with its levels (`...` after the first 10 and before the last
11) and names.

**Interrupts** (H4). `request_interrupt()` sets an atomic flag and installs the hooked
dispatch; the next instruction any thread runs sees the flag (`take_interrupt`) and raises
`interrupted!`. The stand-alone program calls it from its `SIGINT` handler.

## Tests

The suite's `db.lua` (hooks, `getinfo`, locals, tracebacks), `locals.lua`, `coroutine.lua`
(hooks in coroutines) and `errors.lua`; `tests/stdlib_test.cpp` checks `std/debug.lua` against
the library (S1.2).
