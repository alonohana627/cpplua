---@meta

-- The base library's functions are implemented in C++ in lib/std/base.cpp.
-- This file is a reference only (for readers and lua-language-server); lua2 never runs it.

---A global variable holding the global environment. Lua itself does not use it;
---changing its value does not change any environment.
---@type table
_G = _G

---A global variable holding the running Lua version: "Lua 5.4".
---@type string
_VERSION = "Lua 5.4"

---Raises an error if `v` is false or nil; otherwise returns all its arguments.
---The error message is `message` (default "assertion failed!").
---@generic T
---@param v? T
---@param message? any
---@param ... any
---@return T
---@return any ...
function assert(v, message, ...) end

---Controls the garbage collector. `opt` is one of "collect" (default), "stop", "restart",
---"count" (memory in use, in Kbytes), "step", "isrunning", "incremental" or "generational".
---"setpause" and "setstepmul" are deprecated; "incremental"/"generational" return the previous mode.
---@param opt? "collect"|"stop"|"restart"|"count"|"step"|"isrunning"|"incremental"|"generational"|"setpause"|"setstepmul"
---@param ... any
---@return any
function collectgarbage(opt, ...) end

---Opens the named file and runs its contents as a Lua chunk, returning all its results.
---Without `filename`, runs the standard input. Errors propagate to the caller.
---@param filename? string
---@return any ...
function dofile(filename) end

---Raises an error with `message` as the error object; it never returns.
---`level` 1 (default) adds the position where `error` was called, 2 the caller's, 0 none.
---@param message any
---@param level? integer
function error(message, level) end

---Returns the metatable of `object`, or nil. If the metatable has a `__metatable`
---field, returns that value instead.
---@param object any
---@return any
function getmetatable(object) end

---Returns an iterator function, the table `t` and 0, so that `for i, v in ipairs(t)`
---iterates over (1, t[1]), (2, t[2]), ... up to the first absent index.
---@generic V
---@param t V[]|table
---@return fun(t: table, i?: integer): integer, V
---@return table
---@return integer
function ipairs(t) end

---Loads a chunk from a string or from a function returning pieces of it, without running it.
---Returns the compiled function, or nil plus an error message. lua2 accepts only text chunks:
---a binary chunk fails with "bad binary format" even when `mode` allows "b".
---@param chunk string|fun(): string?
---@param chunkname? string
---@param mode? "b"|"t"|"bt"
---@param env? table
---@return function?
---@return string? errmsg
function load(chunk, chunkname, mode, env) end

---Like `load`, but takes the chunk from the file `filename` (or the standard input).
---lua2 accepts only text chunks.
---@param filename? string
---@param mode? "b"|"t"|"bt"
---@param env? table
---@return function?
---@return string? errmsg
function loadfile(filename, mode, env) end

---Returns the next index of `table` after `index` and its value, or nil at the end.
---`next(t)` returns the first entry; the traversal order is unspecified.
---@generic K, V
---@param table table<K, V>
---@param index? K
---@return K?
---@return V?
function next(table, index) end

---If `t` has a `__pairs` metamethod, calls it with `t` and returns its first three results;
---otherwise returns `next`, `t` and nil, so `for k, v in pairs(t)` visits every entry.
---@generic K, V
---@param t table<K, V>
---@return fun(t: table<K, V>, k?: K): K, V
---@return table<K, V>
---@return nil
function pairs(t) end

---Calls `f` with the given arguments in protected mode. Returns true plus f's results,
---or false plus the error object.
---@param f function
---@param ... any
---@return boolean success
---@return any ...
function pcall(f, ...) end

---Writes its arguments to the standard output, converted with `tostring`, separated by tabs
---and followed by a newline. In lua2 the text goes through the interpreter's output function.
---@param ... any
function print(...) end

---Returns whether `v1` equals `v2`, without calling the `__eq` metamethod.
---@param v1 any
---@param v2 any
---@return boolean
function rawequal(v1, v2) end

---Returns `table[index]` without calling the `__index` metamethod.
---@param table table
---@param index any
---@return any
function rawget(table, index) end

---Returns the length of the table or string `v`, without calling the `__len` metamethod.
---@param v table|string
---@return integer
function rawlen(v) end

---Sets `table[index] = value` without calling the `__newindex` metamethod; returns `table`.
---@param table table
---@param index any
---@param value any
---@return table
function rawset(table, index, value) end

---With a number `index`, returns all arguments after argument number `index`
---(a negative index counts from the end). With "#", returns the number of extra arguments.
---@param index integer|"#"
---@param ... any
---@return any
function select(index, ...) end

---Sets the metatable of table `table` (nil removes it) and returns `table`.
---Raises an error if the current metatable has a `__metatable` field.
---@generic T: table
---@param table T
---@param metatable? table
---@return T
function setmetatable(table, metatable) end

---Converts `e` to a number, or returns nil. With `base` (2 to 36), `e` must be a string
---holding an integer numeral in that base; letters stand for digits 10 to 35.
---@overload fun(e: string, base: integer): integer?
---@param e any
---@return number?
function tonumber(e, base) end

---Converts `v` to a human-readable string. Uses the `__tostring` metamethod when present,
---and the `__name` metafield in the default form.
---@param v any
---@return string
function tostring(v) end

---Returns the type of `v` as a string: "nil", "number", "string", "boolean", "table",
---"function", "thread" or "userdata".
---@param v any
---@return "nil"|"number"|"string"|"boolean"|"table"|"function"|"thread"|"userdata"
function type(v) end

---Emits a warning made of all its arguments (strings) concatenated.
---A single argument "@on" or "@off" turns warnings on or off (they start off).
---@param msg string
---@param ... string
function warn(msg, ...) end

---Like `pcall`, but calls the message handler `msgh` with the error object when an error
---happens; its result becomes the returned error object.
---@param f function
---@param msgh function
---@param ... any
---@return boolean success
---@return any ...
function xpcall(f, msgh, ...) end
