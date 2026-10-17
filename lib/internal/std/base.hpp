#pragma once

#include "gc/heap.hpp"

namespace lua2 {

// The standard library (roadmap stage 9) starts here, early: the base
// library's `print`, so that programs can show what they do. More of the
// base library, and the other libraries, come with stage 9.

// Puts the base library's functions in the globals table (luaopen_base):
// for now only `print`.
void open_base(Heap& heap);

} // namespace lua2
