#include "vm/run.hpp"

#include <format>
#include <string>

#include "gc/heap.hpp"
#include "parser/parser.hpp"
#include "value/convert.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "vm/compiler.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"

namespace lua2 {

std::expected<void, std::string> load(State& state, std::string_view source,
                                      std::string_view chunk_name) {
    Parser parser{source, chunk_name};
    auto ast = parser.parse_chunk();
    if (!ast) {
        return std::unexpected(std::move(ast.error().message));
    }
    Heap& heap = state.heap();
    const std::string lua_source = "@" + std::string{chunk_name};
    auto proto = compile(heap, *ast, lua_source, chunk_name);
    if (!proto) {
        return std::unexpected(std::move(proto.error().message));
    }
    // The main closure, with its _ENV upvalue, the globals table (E1.2;
    // luaF_initupvals, then lua_load sets it). Nothing can be collected
    // before it is on the stack: collection only runs when the VM checks.
    LuaClosure* closure = heap.new_closure(*proto, 1);
    Upvalue* env = heap.new_upvalue();
    env->set(Value::table(&heap.globals()));
    closure->upvalues()[0] = env;
    if (!state.check_stack(1)) {
        return std::unexpected(std::move(state.error_message));
    }
    state.push(Value::closure(closure));
    return {};
}

std::expected<std::vector<Value>, std::string> run(State& state, std::string_view source,
                                                   std::string_view chunk_name) {
    const std::uint32_t func = state.top;
    if (auto loaded = load(state, source, chunk_name); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    if (call(state, func, all_results) != Status::Ok) {
        // Unwind what the error left: close the upvalues of the dead frames
        // and go back to the bottom frame (stage 8 does this per pcall).
        state.close_upvalues(func);
        state.ci = &state.base_ci;
        state.top = func;
        return std::unexpected(std::move(state.error_message));
    }
    std::vector<Value> results;
    results.reserve(state.top - func);
    for (std::uint32_t slot = func; slot < state.top; ++slot) {
        results.push_back(state.get(slot));
    }
    state.top = func;
    return results;
}

std::string display_text(Value value) {
    switch (value.tag()) {
    case Tag::Nil: return "nil";
    case Tag::False: return "false";
    case Tag::True: return "true";
    case Tag::Integer:
    case Tag::Float: return std::string{number_text(value).value_or(NumberText{}).view()};
    case Tag::ShortString:
    case Tag::LongString: return std::string{value.as_string()->view()};
    case Tag::CFunction:
    case Tag::LuaClosure:
    case Tag::Table:
        // "%s: %p" (luaL_tolstring), with the address the way the C library's
        // %p writes it: 16 hex digits on Windows, 0x and the digits elsewhere.
#ifdef _WIN32
        return std::format("{}: {:016x}", type_name(value.type()), value.payload());
#else
        return std::format("{}: 0x{:x}", type_name(value.type()), value.payload());
#endif
    }
    return "?";
}

} // namespace lua2
