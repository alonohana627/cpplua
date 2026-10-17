#pragma once

#include "gc/heap.hpp"
#include "vm/state.hpp"

namespace lua2 {

// The standard library so far (docs/final_docs/stdlib.md): the base
// library's functions that tables, metatables and errors need (stages 6 to
// 8), and debug.traceback. The rest comes with stage 9.

// Puts the base library's functions in the globals table (luaopen_base):
// print, type, tostring, tonumber, next, pairs, ipairs, select, rawget,
// rawset, rawequal, rawlen, setmetatable, getmetatable, error, assert,
// pcall, xpcall, collectgarbage, and _G and _VERSION.
void open_base(Heap& heap);

// The debug library, for now only debug.traceback (R4).
void open_debug(Heap& heap);

// The iterators of pairs (next) and ipairs: the VM runs them itself for
// plain tables in a generic for, and knows them by address.
int base_next(State& state);
int base_ipairs_step(State& state);

} // namespace lua2
