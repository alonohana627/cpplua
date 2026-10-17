---@meta

-- The debug library's functions are implemented in C++ in lib/std/base.cpp.
-- This file is a reference only (for readers and lua-language-server); lua2 never runs it.

---The debug library. lua2 has only `debug.traceback` so far.
---@class debuglib
debug = {}

---Returns a traceback of the call stack, starting at `level` (default 1, the function calling
---`traceback`), preceded by `message` when given. A `message` that is neither a string, a number
---nor nil is returned unchanged. lua2 does not take the optional thread first argument yet.
---@param message? any
---@param level? integer
---@return any
function debug.traceback(message, level) end
