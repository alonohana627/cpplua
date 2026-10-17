---@meta

-- The table library's functions are implemented in C++ in lib/std/table.cpp.
-- This file is a reference only (for readers and lua-language-server); cpplua never runs it.

---The table library. Its functions respect metamethods (`__index`, `__newindex`, `__len`).
---@class tablelib
table = {}

---Returns `list[i] .. sep .. list[i+1] ... sep .. list[j]`. `sep` defaults to "",
---`i` to 1 and `j` to `#list`. Every element must be a string or a number.
---@param list table
---@param sep? string
---@param i? integer
---@param j? integer
---@return string
function table.concat(list, sep, i, j) end

---Inserts `value` at position `pos` in `list`, shifting up the elements after it.
---Without `pos`, appends `value` at the end (`#list + 1`).
---@overload fun(list: table, value: any)
---@param list table
---@param pos integer
---@param value any
function table.insert(list, pos, value) end

---Moves elements `a1[f]` to `a1[e]` into `a2[t]` onward (`a2` defaults to `a1`;
---overlapping ranges are handled). Returns `a2`.
---@param a1 table
---@param f integer
---@param e integer
---@param t integer
---@param a2? table
---@return table a2
function table.move(a1, f, e, t, a2) end

---Returns a new table with all arguments stored at keys 1, 2, ... and field `n`
---holding the number of arguments.
---@param ... any
---@return table
function table.pack(...) end

---Removes the element at `pos` (default `#list`) from `list`, shifting down the elements
---after it, and returns the removed value.
---@param list table
---@param pos? integer
---@return any
function table.remove(list, pos) end

---Sorts `list[1]` to `list[#list]` in place. `comp(a, b)` must return true when `a` goes
---before `b` (default `<`). The sort is not stable.
---@generic T
---@param list T[]
---@param comp? fun(a: T, b: T): boolean
function table.sort(list, comp) end

---Returns the elements `list[i]` to `list[j]`; `i` defaults to 1 and `j` to `#list`.
---@param list table
---@param i? integer
---@param j? integer
---@return any ...
function table.unpack(list, i, j) end
