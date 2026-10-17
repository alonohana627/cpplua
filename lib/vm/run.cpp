#include "vm/run.hpp"

#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <cpplua/source_file.hpp>

#include "gc/heap.hpp"
#include "parser/parser.hpp"
#include "std/auxlib.hpp"
#include "value/convert.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "vm/compiler.hpp"
#include "vm/dump.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/proto.hpp"

namespace cpplua {

std::expected<void, std::string> load(State& state, std::string_view source,
                                      std::string_view chunk_name) {
    return load_chunk(state, source, "@" + std::string{chunk_name}, "bt");
}

std::expected<void, std::string> load_chunk(State& state, std::string_view source,
                                            std::string_view source_name, std::string_view mode) {
    // checkmode: a binary chunk starts with "\x1bLua".
    const bool binary = source.starts_with('\x1b');
    if (!mode.contains(binary ? 'b' : 't')) {
        return std::unexpected(std::format("attempt to load a {} chunk (mode is '{}')",
                                           binary ? "binary" : "text", mode));
    }
    Heap& heap = state.heap();
    if (binary) { // cpplua's binary chunks are Lua 5.4's (luaU_undump)
        auto closure = undump(heap, source, source_name);
        if (!closure) {
            return std::unexpected(std::move(closure.error()));
        }
        if (!(*closure)->upvalues().empty()) { // the first upvalue: the globals
            (*closure)->upvalues()[0]->set(Value::table(&heap.globals()));
        }
        if (!state.check_stack(1)) {
            return std::unexpected(std::string{"stack overflow"});
        }
        state.push(Value::closure(*closure));
        return {};
    }
    const std::string chunk_name = chunk_id(source_name);
    Parser parser{source, chunk_name};
    auto ast = parser.parse_chunk();
    if (!ast) {
        return std::unexpected(std::move(ast.error().message));
    }
    auto proto = compile(heap, *ast, source_name, chunk_name);
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
        return std::unexpected(std::string{"stack overflow"});
    }
    state.push(Value::closure(closure));
    return {};
}

namespace {

// skipcomment: a file without its byte order mark and '#' line; the line's
// newline stays (for line numbers), except before a binary chunk.
std::string chunk_text(std::string_view file) {
    std::string_view text = skip_file_prefix(file);
    if (text.starts_with("\n\x1b") && text.size() < file.size()) {
        text.remove_prefix(1);
    }
    return std::string{text};
}

} // namespace

std::expected<std::string, std::string> read_chunk_file(const std::string& path) {
    if (path.empty()) {
        const std::string text{std::istreambuf_iterator<char>{std::cin},
                               std::istreambuf_iterator<char>{}};
        return chunk_text(text);
    }
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): the message of the open that failed
        return std::unexpected(std::format("cannot open {}: {}", path, std::strerror(errno)));
    }
    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    return chunk_text(text);
}

int traceback_handler(State& state) { // NOLINT(misc-const-correctness)
    const Value error = arg(state, 1);
    std::string message;
    if (error.is_string() || error.is_number()) {
        message = display_text(error);
    } else {
        Value text;
        if (!metafield(state.heap(), error, "__tostring").is_nil() &&
            to_display_string(state, error, text) == Status::Ok && text.is_string()) {
            push(state, text);
            return 1;
        }
        message = std::format("(error object is a {} value)", type_name(error.type()));
    }
    push(state, Value::string(state.heap().new_string(traceback(state, message, 1))));
    return 1;
}

namespace {

// The C++ function that runs a chunk, as lua.c's pmain does: so a traceback
// ends with "[C]: in ?", as the real Lua's.
int run_chunk(State& state) { // NOLINT(misc-const-correctness)
    const std::uint32_t chunk = arg_slot(state, 1);
    if (call(state, chunk, all_results) != Status::Ok) {
        return c_function_error;
    }
    return static_cast<int>(state.top - chunk);
}

// Loads the chunk and runs it protected; the results (or the error object)
// start at `base`.
std::expected<void, std::string> run_protected(State& state, std::string_view source,
                                               std::string_view chunk_name, std::uint32_t base,
                                               ErrorReport report) {
    if (!state.check_stack(2)) {
        return std::unexpected(std::string{"stack overflow"});
    }
    state.push(Value::c_function(&run_chunk));
    if (auto loaded = load(state, source, chunk_name); !loaded) {
        state.top = base;
        return std::unexpected(std::move(loaded.error()));
    }
    const Value handler =
        report == ErrorReport::Traceback ? Value::c_function(&traceback_handler) : Value::nil();
    if (protected_call(state, base, all_results, handler) != Status::Ok) {
        std::string text = error_text(state, state.get(base));
        state.top = base;
        return std::unexpected(std::move(text));
    }
    return {};
}

} // namespace

std::expected<std::vector<std::string>, std::string>
run_text(State& state, std::string_view source, std::string_view chunk_name, ErrorReport report) {
    const std::uint32_t base = state.top;
    if (auto ran = run_protected(state, source, chunk_name, base, report); !ran) {
        return std::unexpected(std::move(ran.error()));
    }
    // The results are on the stack, anchored, while __tostring runs.
    std::vector<std::string> texts;
    const std::uint32_t end = state.top;
    for (std::uint32_t slot = base; slot < end; ++slot) {
        Value text;
        if (to_display_string(state, state.get(slot), text) != Status::Ok) {
            std::string error = error_text(state, state.error_value);
            state.top = base;
            return std::unexpected(std::move(error));
        }
        texts.emplace_back(text.as_string()->view());
    }
    state.top = base;
    return texts;
}

std::expected<std::vector<Value>, std::string> run(State& state, std::string_view source,
                                                   std::string_view chunk_name) {
    const std::uint32_t base = state.top;
    if (auto ran = run_protected(state, source, chunk_name, base, ErrorReport::Message); !ran) {
        return std::unexpected(std::move(ran.error()));
    }
    std::vector<Value> results;
    results.reserve(state.top - base);
    for (std::uint32_t slot = base; slot < state.top; ++slot) {
        results.push_back(state.get(slot));
    }
    state.top = base;
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
    case Tag::CClosure:
    case Tag::LightUserdata:
    case Tag::Userdata:
    case Tag::Thread:
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

} // namespace cpplua
