#include "std/libs.hpp"

#include "std/base.hpp"

namespace cpplua {

void open_libraries(Heap& heap) {
    // In luaL_openlibs's order (linit.c).
    open_base(heap);
    open_package(heap);
    open_coroutine(heap);
    open_table(heap);
    open_io(heap);
    open_os(heap);
    open_string(heap);
    open_math(heap);
    open_utf8(heap);
    open_debug(heap);
}

} // namespace cpplua
