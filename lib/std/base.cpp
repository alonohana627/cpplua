#include "std/base.hpp"

#include <cstdint>
#include <string>

#include "value/table.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace lua2 {

namespace {

// print(...): every argument as tostring shows it, separated by tabs, then a
// newline (luaB_print). Returns nothing.
// The signature is CFunction's: C++ functions may change the thread.
int print(State& state) { // NOLINT(misc-const-correctness,readability-non-const-parameter)
    const std::uint32_t first = state.ci->func + 1;
    std::string line;
    for (std::uint32_t slot = first; slot < state.top; ++slot) {
        if (slot > first) {
            line += '\t';
        }
        line += display_text(state.get(slot));
    }
    line += '\n';
    state.heap().write_output(line);
    return 0;
}

} // namespace

void open_base(Heap& heap) {
    heap.globals().set(heap, heap.new_string("print"), Value::c_function(&print));
}

} // namespace lua2
