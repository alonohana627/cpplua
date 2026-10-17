---@meta

-- The debug library's functions are implemented in C++ in lib/std/debug.cpp, and the hooks run
-- in the VM (lib/vm/execute.cpp). This file is a reference only (for readers and
-- lua-language-server); cpplua never runs it.

---The debug library: Lua 5.4's whole debug interface. Use it for debugging and introspection
---only; it can break assumptions that ordinary code relies on (local variables, upvalues,
---protected metatables). Most functions take an optional thread first: they then work on that
---thread's call stack instead of the running one's. A stack level is 0 for the debug function
---itself, 1 for the function calling it, and so on.
---@class debuglib
debug = {}

---Enters an interactive mode: reads lines from the standard input and runs each as a Lua chunk,
---printing errors to the standard error, until a line holding only "cont".
function debug.debug() end

---Returns the `n`-th user value (default 1) of userdata `u` plus true, or nil when `u` is not a
---full userdata or has no such value.
---@param u any
---@param n? integer
---@return any
---@return boolean? found
function debug.getuservalue(u, n) end

---Returns the hook function, mask and count of the thread (default: the running one), as set by
---`debug.sethook`; nil when it has no hook.
---@overload fun(thread: thread): function?, string, integer
---@return function? hook
---@return string mask
---@return integer count
function debug.gethook() end

---Returns a table about a function: `f` is a function or a stack level (nil when the level is
---out of range). `what` picks the fields (default "flnSrtu", all but "L"): "S" source,
---short_src, linedefined, lastlinedefined, what; "l" currentline; "u" nups, nparams, isvararg;
---"n" name, namewhat; "r" ftransfer, ntransfer; "t" istailcall; "L" activelines; "f" func.
---@overload fun(thread: thread, f: integer|function, what?: string): table?
---@param f integer|function
---@param what? string
---@return table?
function debug.getinfo(f, what) end

---Returns the name and value of local variable `index` of the function at stack `level`, or nil.
---Negative indices give the varargs; unnamed slots are "(temporary)" or "(C temporary)". With a
---function instead of a level, returns only the name of its `index`-th parameter.
---@overload fun(thread: thread, level: integer|function, index: integer): string?, any
---@param level integer|function
---@param index integer
---@return string? name
---@return any value
function debug.getlocal(level, index) end

---Returns the registry table.
---@return table
function debug.getregistry() end

---Returns the metatable of `value` (any type), ignoring `__metatable`, or nil.
---@param value any
---@return table?
function debug.getmetatable(value) end

---Returns the name and value of upvalue `up` of function `f`, or nothing if there is none.
---C++ functions' upvalues have the name "".
---@param f function
---@param up integer
---@return string? name
---@return any value
function debug.getupvalue(f, up) end

---Makes upvalue `n1` of Lua function `f1` refer to upvalue `n2` of Lua function `f2`.
---@param f1 function
---@param n1 integer
---@param f2 function
---@param n2 integer
function debug.upvaluejoin(f1, n1, f2, n2) end

---Returns a unique identifier (a light userdata) for upvalue `n` of function `f`, or nil if it
---has none. Two closures share an upvalue exactly when its identifiers are equal.
---@param f function
---@param n integer
---@return lightuserdata?
function debug.upvalueid(f, n) end

---Sets the `n`-th user value (default 1) of full userdata `udata` to `value` and returns
---`udata`, or nil when it has no such value.
---@param udata userdata
---@param value any
---@param n? integer
---@return userdata?
function debug.setuservalue(udata, value, n) end

---Sets `hook` as the hook of the thread (default: the running one); no arguments turn it off.
---`mask` holds "c" (called on each call, also "tail call"), "r" (on each return) and "l" (on
---each new line); a `count` above 0 also calls it every `count` instructions. The hook gets the
---event name and, for "line", the line. In cpplua the hooks are Lua functions kept in the
---registry's _HOOKKEY table (weak in its keys, the threads); they cost nothing while no thread
---has one; no hook runs inside a hook, and a hook function cannot yield.
---@overload fun()
---@overload fun(thread: thread)
---@overload fun(thread: thread, hook: function, mask: string, count?: integer)
---@param hook function
---@param mask string
---@param count? integer
function debug.sethook(hook, mask, count) end

---Assigns `value` to local variable `index` of the function at stack `level` and returns its
---name, or nil when there is no such local. Raises an error if `level` is out of range.
---@overload fun(thread: thread, level: integer, index: integer, value: any): string?
---@param level integer
---@param index integer
---@param value any
---@return string?
function debug.setlocal(level, index, value) end

---Sets the metatable of `value` (nil removes it) and returns `value`, ignoring `__metatable`.
---For a value that is neither a table nor a userdata, it sets the metatable of its whole type.
---@generic T
---@param value T
---@param table? table
---@return T
function debug.setmetatable(value, table) end

---Assigns `value` to upvalue `up` of function `f` and returns its name, or nothing if there is
---no such upvalue.
---@param f function
---@param up integer
---@param value any
---@return string?
function debug.setupvalue(f, up, value) end

---Returns a traceback of the call stack, starting at `level` (default 1, the function calling
---`traceback`; 0 for another thread), preceded by `message` when given. A `message` that is
---neither a string, a number nor nil is returned unchanged.
---@overload fun(thread: thread, message?: any, level?: integer): any
---@param message? any
---@param level? integer
---@return any
function debug.traceback(message, level) end

---Deprecated in Lua 5.4: changes nothing and returns 200, cpplua's limit of nested C++ calls.
---@param limit integer
---@return integer
function debug.setcstacklimit(limit) end
