#include <lua2/interpreter.hpp>

#include "gc/heap.hpp"
#include "std/base.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace lua2 {

Interpreter::Interpreter() : heap_(std::make_unique<Heap>()) {
    open_base(*heap_);
}

Interpreter::Interpreter(Interpreter&&) noexcept = default;
Interpreter& Interpreter::operator=(Interpreter&&) noexcept = default;
Interpreter::~Interpreter() = default;

std::expected<std::vector<std::string>, std::string> Interpreter::run(std::string_view source,
                                                                      std::string_view chunk_name) {
    auto results = lua2::run(heap_->main_thread(), source, chunk_name);
    if (!results) {
        return std::unexpected(std::move(results.error()));
    }
    // As text right away: nothing anchors the values any more.
    std::vector<std::string> texts;
    texts.reserve(results->size());
    for (const Value value : *results) {
        texts.push_back(display_text(value));
    }
    return texts;
}

void Interpreter::collect() {
    heap_->collect();
}

void Interpreter::set_generational(bool generational) {
    heap_->set_mode(generational ? GcMode::Generational : GcMode::Incremental);
}

std::size_t Interpreter::memory_in_use() const {
    return heap_->total_bytes();
}

} // namespace lua2
