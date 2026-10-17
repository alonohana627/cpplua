---@meta

-- The coroutine library's functions are implemented in C++ in lib/std/coroutine.cpp, and the
-- coroutines themselves in the VM (lib/vm/execute.cpp). This file is a reference only (for
-- readers and lua-language-server); lua2 never runs it.

---The coroutine library. A coroutine is a function that can stop (yield) and later go on
---(resume) where it stopped; its value has type "thread".
---@class coroutinelib
coroutine = {}

---Creates a new coroutine with body `f`, suspended until its first resume.
---@param f function
---@return thread
function coroutine.create(f) end

---Starts or continues coroutine `co`. The first resume passes the arguments to its body; later
---ones become the results of the `yield` it stopped in. Returns true and what the coroutine
---yields or returns, or false and the error object if it fails (it is then dead).
---@param co thread
---@param ... any
---@return boolean ok
---@return any ...
function coroutine.resume(co, ...) end

---Returns the running coroutine and whether it is the main thread.
---@return thread running
---@return boolean ismain
function coroutine.running() end

---Returns the status of `co`: "running", "suspended" (yielded or not started), "normal" (it
---resumed another coroutine) or "dead" (finished, or stopped by an error).
---@param co thread
---@return "running"|"suspended"|"normal"|"dead"
function coroutine.status(co) end

---Creates a coroutine with body `f` and returns a function that resumes it each time it is
---called, returning what it yields or returns. An error is raised in the caller (a string error
---gets the caller's position in front), after the coroutine's variables are closed.
---@param f function
---@return function
function coroutine.wrap(f) end

---Stops the running coroutine; the values go to the `resume` that ran it, and what the next
---`resume` passes becomes the results of `yield`. Yields work across metamethods, iterators,
---`pcall` and `xpcall`, but not across other C++ functions ("attempt to yield across a C-call
---boundary") nor outside a coroutine.
---@param ... any
---@return any ...
function coroutine.yield(...) end

---Returns whether coroutine `co` (default: the running one) can yield: it is not the main
---thread and not inside a C++ function that cannot be yielded across.
---@param co? thread
---@return boolean
function coroutine.isyieldable(co) end

---Closes a suspended or dead coroutine: its pending to-be-closed variables close, and it
---becomes dead. Returns true, or false and the error object if it had died of an error or a
---`__close` fails.
---@param co thread
---@return boolean ok
---@return any err
function coroutine.close(co) end
