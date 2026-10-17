#pragma once

#include "gc/heap.hpp"

namespace cpplua {

// The libraries of the standard library (docs/final_docs/stdlib.md), all in
// C++. Each open_ function makes its library a global and a loaded module
// (luaL_requiref); open_libraries opens them all (luaL_openlibs).

void open_string(Heap& heap);
void open_table(Heap& heap);
void open_coroutine(Heap& heap);
void open_math(Heap& heap);
void open_utf8(Heap& heap);
void open_io(Heap& heap);
void open_os(Heap& heap);
void open_package(Heap& heap);
void open_debug(Heap& heap);

void open_libraries(Heap& heap);

} // namespace cpplua
