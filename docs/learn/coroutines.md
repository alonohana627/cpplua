# Coroutines and hooks: background

The ideas behind roadmap stage 10, for reading the code. How lua2 does each is in
[../final_docs/coroutines.md](../final_docs/coroutines.md) and
[../final_docs/debug.md](../final_docs/debug.md).

## 1. A coroutine is a second stack

A coroutine is a function that can stop in the middle (`coroutine.yield`) and be continued
later (`coroutine.resume`), keeping its locals and where it was. Lua's coroutines are
*asymmetric* (a coroutine yields back to whoever resumed it, not to any other coroutine) and
*stackful* (it can yield from inside functions it called, however deep). Stackful means each
coroutine needs a whole call stack of its own, so a coroutine is a thread in Lua's sense: a
`lua_State` with its own value stack and its own list of call frames, sharing the heap and the
globals with the others. Nothing runs in parallel; "thread" only means "a separate stack".

## 2. Lua frames are easy to stop

A Lua function's frame lives in the thread's value stack and its `CallInfo`: registers, the
program counter, the number of results wanted. The VM does not keep any of that on the machine
(C) stack between instructions. So stopping a Lua function is just leaving the VM loop, and
continuing it is entering the VM loop again with the same `CallInfo`: everything it needs is
still there. That is why yields from Lua code are cheap and need nothing special.

## 3. C frames are the hard part

C functions do live on the machine stack. If Lua calls `pcall` (a C function), which calls a
Lua function, which yields, then `pcall`'s C frame is in the middle of the machine stack between
the resume and the yield. When the coroutine stops, that machine stack must be given back to
the resumer; the C frame cannot be kept. Lua's answer (since 5.2) is the **continuation**: a C
function that may be yielded across gives Lua a second function (`lua_callk`, `lua_pcallk`) to
call *instead of returning* to the first one. When the coroutine is resumed, Lua calls the
continuation with the results, and it finishes the job. A C function without one cannot be
yielded across, which is the error `attempt to yield across a C-call boundary`.

Metamethods have the same problem inside the VM: `a + b` calling `__add`, which yields, stops
in the middle of the `ADD` instruction's C code. Lua remembers nothing about it; on resume it
looks at the interrupted instruction and finishes it with the metamethod's result
(`luaV_finishOp`): put the result in the right register, or decide the comparison's jump.

## 4. How Lua leaves the machine stack: `longjmp`, or return values

The real Lua unwinds the machine stack on a yield with `longjmp`, as it does for errors: the
resume did a `setjmp`, and the yield jumps straight back to it, dropping every C frame in
between (which is why they need continuations). lua2 has no `longjmp` (stage 8 made errors
return values): a yield is `Status::Yield`, returned up through every C++ frame. Each frame on
the way does what Lua's `longjmp` would have done to it, which is nothing: it returns, and its
Lua-side state stays as it was. The result is the same, and every place that can yield is
visible in the code as a status check.

Other designs exist. C++20 coroutines are stackless (one function at a time), so they cannot
yield from deep calls without making every function a coroutine. Fibers or `ucontext` give
each coroutine its own machine stack, which costs memory per coroutine and does not work
everywhere. Lua's design needs neither.

## 5. Errors and yields together

A `pcall` that was yielded across has lost its C frame, and with it the `setjmp` (or, in lua2,
the C++ code) that would catch an error. So after a resume, an error looks for the innermost
`pcall` that yielded (Lua marks its frame `CIST_YPCALL`) and recovers there: closes what must
be closed, then calls the `pcall`'s continuation with the error status (`precover`). The
to-be-closed variables a `pcall` closes may themselves yield, so closing must also be
resumable.

## 6. Hooks

A hook is a function the VM calls on events: a call, a return, a new line, every N
instructions. Debuggers and profilers are built on them (`debug.sethook`). Checking for hooks
costs something on every instruction; Lua checks a flag in each instruction's dispatch. lua2's
VM dispatches through a table of handlers, so it swaps the table instead: while no thread has a
hook, the VM runs the same code as if hooks did not exist.

Line hooks show how a VM keeps source positions: each instruction has a line (Lua stores the
difference from the previous one in a byte), and the hook runs when the line changes or the
program jumps backwards (a new loop iteration on the same line).
