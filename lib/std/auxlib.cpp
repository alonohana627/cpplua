// The helpers of the standard library: a port of the parts of Lua 5.4's
// lauxlib.c (and of lapi.c and ldebug.c behind them) that the library needs.

#include "std/auxlib.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <system_error>

#include "gc/heap.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "vm/debug_info.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/proto.hpp"
#include "vm/run.hpp"

namespace cpplua {

const Proto& frame_proto(const State& state, const CallInfo* frame) {
    return *std::bit_cast<const LuaClosure*>(state.stack_values[frame->func])->proto;
}

int frame_pc(const State& state, const CallInfo* frame) {
    return static_cast<int>(frame->saved_pc - frame_proto(state, frame).code.data()) - 1;
}

namespace {

const Proto& proto_of(const State& state, const CallInfo* frame) {
    return frame_proto(state, frame);
}

// lua_getinfo's "S" and "l": the chunk as messages show it, and the line the
// frame is at (-1 for C++ functions).
struct FrameInfo {
    std::string short_source;
    int current_line = -1;
    int line_defined = -1;
    bool is_main = false;
    bool is_lua = false;
};

FrameInfo frame_info(const State& state, const CallInfo* frame) {
    if (!state.is_lua(frame)) {
        return {.short_source = "[C]"};
    }
    const Proto& p = proto_of(state, frame);
    return {
        .short_source = chunk_id(p.source->view()),
        .current_line = line_of(p, frame_pc(state, frame)),
        .line_defined = p.line_defined,
        .is_main = p.line_defined == 0,
        .is_lua = true,
    };
}

} // namespace

// ---- Arguments ----

int arg_count(const State& state) {
    return static_cast<int>(state.top - state.ci->func - 1);
}

Value arg(const State& state, int n) {
    if (n > arg_count(state)) {
        return Value::nil();
    }
    return state.get(arg_slot(state, n));
}

bool arg_is_none(const State& state, int n) {
    return n > arg_count(state);
}

bool arg_is_none_or_nil(const State& state, int n) {
    return arg(state, n).is_nil();
}

std::uint32_t arg_slot(const State& state, int n) {
    return state.ci->func + static_cast<std::uint32_t>(n);
}

void push(State& state, Value v) {
    state.push(v);
}

// ---- Errors ----

std::string where(const State& state, int level) {
    if (const CallInfo* frame = state.frame_at(level)) {
        const FrameInfo info = frame_info(state, frame);
        if (info.current_line > 0) {
            return std::format("{}:{}: ", info.short_source, info.current_line);
        }
    }
    return {};
}

int raise_error(State& state, std::string_view message) {
    state.set_error(where(state, 1) + std::string{message});
    return c_function_error;
}

int arg_error(State& state, int n, std::string_view extra) {
    // luaL_argerror: the function named as its caller calls it.
    FunctionName name = function_name(state, state.ci);
    if (name.kind == "method") {
        --n; // self does not count
        if (n == 0) {
            return raise_error(state,
                               std::format("calling '{}' on bad self ({})", name.name, extra));
        }
    }
    if (name.kind.empty()) {
        name.name = global_function_name(state, state.get(state.ci->func)).value_or("?");
    }
    return raise_error(state, std::format("bad argument #{} to '{}' ({})", n, name.name, extra));
}

int arg_type_error(State& state, int n, std::string_view expected) {
    // luaL_typeerror: the argument's type, or its __name.
    const Value v = arg(state, n);
    std::string got;
    const Value name = metafield(state.heap(), v, "__name");
    if (name.is_string()) {
        got = name.as_string()->view();
    } else if (v.is_light_userdata()) {
        got = "light userdata";
    } else if (arg_is_none(state, n)) {
        got = "no value";
    } else {
        got = type_name(v.type());
    }
    return arg_error(state, n, std::format("{} expected, got {}", expected, got));
}

bool check_any(State& state, int n) {
    if (arg_is_none(state, n)) {
        arg_error(state, n, "value expected");
        return false;
    }
    return true;
}

bool check_table(State& state, int n, Table*& out) {
    const Value v = arg(state, n);
    if (!v.is_table()) {
        arg_type_error(state, n, "table");
        return false;
    }
    out = std::bit_cast<Table*>(v.payload());
    return true;
}

bool check_integer(State& state, int n, std::int64_t& out) {
    // lua_tointegerx: numbers and numeric strings with an integer value.
    Value v = arg(state, n);
    if (v.is_string()) {
        if (const auto number = string_to_number(v.as_string()->view())) {
            v = *number;
        }
    }
    if (const auto i = to_integer(v)) {
        out = *i;
        return true;
    }
    if (v.is_number()) {
        arg_error(state, n, "number has no integer representation");
    } else {
        arg_type_error(state, n, "number");
    }
    return false;
}

bool check_number(State& state, int n, double& out) {
    // lua_tonumberx: numbers and numeric strings, as a float.
    Value v = arg(state, n);
    if (v.is_string()) {
        if (const auto number = string_to_number(v.as_string()->view())) {
            v = *number;
        }
    }
    if (const auto f = to_float(v)) {
        out = *f;
        return true;
    }
    arg_type_error(state, n, "number");
    return false;
}

int file_result(State& state, bool ok, const char* name) {
    const int en = errno;
    if (ok) {
        push(state, Value::boolean(true));
        return 1;
    }
    return error_result(state, en, name);
}

int error_result(State& state, int en, const char* name) {
    push(state, Value::nil());
    const std::string message =
        en != 0 ? std::generic_category().message(en) : std::string{"(no extra info)"};
    push_string(state, name != nullptr ? std::format("{}: {}", name, message) : message);
    push(state, Value::integer(en));
    return 3;
}

int exec_result(State& state, int stat) {
    if (stat != 0 && errno != 0) { // an error with an errno
        return file_result(state, false, nullptr);
    }
    // Windows has no signals: the status is always an exit code.
    push(state, stat == 0 ? Value::boolean(true) : Value::nil());
    push_string(state, "exit");
    push(state, Value::integer(stat));
    return 3;
}

std::string c_format_number(const char* form, double value) {
    std::array<char, 128> buffer{};
#ifdef __MINGW32__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int n = __mingw_snprintf(buffer.data(), buffer.size(), form, value);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int n = std::snprintf(buffer.data(), buffer.size(), form, value);
#endif
    return n < 0 ? std::string{}
                 : std::string{buffer.data(), std::min<std::size_t>(static_cast<std::size_t>(n),
                                                                    buffer.size() - 1)};
}

Table* table_object(Value v) {
    return std::bit_cast<Table*>(v.payload());
}

Status get_index(State& state, Value t, std::int64_t i, Value& out) {
    if (t.is_table()) {
        const Table::Slot slot = table_object(t)->get_int(i);
        if (!slot.empty()) {
            out = slot.value();
            return Status::Ok;
        }
    }
    return index_value(state, t, Value::integer(i), out);
}

Status set_index(State& state, Value t, std::int64_t i, Value v) {
    if (t.is_table()) {
        Table* table = table_object(t);
        const Table::Slot slot = table->get_int(i);
        if (!slot.empty()) { // luaV_fastseti: a present key ignores __newindex
            slot.store(v);
            state.heap().barrier_back(table, v);
            return Status::Ok;
        }
    }
    return assign_value(state, t, Value::integer(i), v);
}

Status length_integer(State& state, Value v, std::int64_t& out) {
    Value n;
    if (length_value(state, v, n) != Status::Ok) {
        return Status::Error;
    }
    if (n.is_string()) { // lua_tointegerx converts strings
        n = string_to_number(n.as_string()->view()).value_or(Value::nil());
    }
    if (const auto i = to_integer(n)) {
        out = *i;
        return Status::Ok;
    }
    raise_error(state, "object length is not an integer");
    return Status::Error;
}

bool check_view(State& state, int n, std::string_view& out) {
    const Value v = arg(state, n);
    if (v.is_number()) {
        state.set(arg_slot(state, n), Value::string(state.heap().new_string(display_text(v))));
    } else if (!v.is_string()) {
        arg_type_error(state, n, "string");
        return false;
    }
    out = arg(state, n).as_string()->view();
    return true;
}

bool opt_view(State& state, int n, std::string_view fallback, std::string_view& out) {
    if (arg_is_none_or_nil(state, n)) {
        out = fallback;
        return true;
    }
    return check_view(state, n, out);
}

bool opt_number(State& state, int n, double fallback, double& out) {
    if (arg_is_none_or_nil(state, n)) {
        out = fallback;
        return true;
    }
    return check_number(state, n, out);
}

bool check_option(State& state, int n, std::string_view fallback,
                  std::span<const std::string_view> options, std::size_t& out) {
    std::string_view name;
    if (!(fallback.empty() ? check_view(state, n, name) : opt_view(state, n, fallback, name))) {
        return false;
    }
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i] == name) {
            out = i;
            return true;
        }
    }
    arg_error(state, n, std::format("invalid option '{}'", name));
    return false;
}

bool check_function(State& state, int n) {
    if (!arg(state, n).is_function()) {
        arg_type_error(state, n, "function");
        return false;
    }
    return true;
}

void push_string(State& state, std::string_view s) {
    state.push(Value::string(state.heap().new_string(s)));
}

void set_functions(Heap& heap, Table& table, std::span<const LibraryFunction> functions) {
    for (const LibraryFunction& f : functions) {
        table.set(heap, Value::string(heap.new_string(f.name)), Value::c_function(f.function));
    }
}

Table* open_library(Heap& heap, std::string_view name, std::span<const LibraryFunction> functions) {
    Table* library = heap.new_table();
    const Value key = Value::string(heap.new_string(name));
    heap.globals().set(heap, key, Value::table(library));
    heap.loaded().set(heap, key, Value::table(library));
    set_functions(heap, *library, functions);
    return library;
}

bool opt_integer(State& state, int n, std::int64_t fallback, std::int64_t& out) {
    if (arg_is_none_or_nil(state, n)) {
        out = fallback;
        return true;
    }
    return check_integer(state, n, out);
}

bool check_string(State& state, int n, std::string& out) {
    // luaL_checklstring: strings, and numbers as text.
    const Value v = arg(state, n);
    if (v.is_string()) {
        out = v.as_string()->view();
        return true;
    }
    if (v.is_number()) {
        out = display_text(v);
        return true;
    }
    arg_type_error(state, n, "string");
    return false;
}

// ---- Conversions ----

Status to_display_string(State& state, Value v, Value& out) {
    // luaL_tolstring
    Heap& heap = state.heap();
    const Value tm = metafield(heap, v, "__tostring");
    if (!tm.is_nil()) {
        // luaL_callmeta: tm(v), one result.
        if (!state.check_stack(3)) {
            return runtime_error(state, "stack overflow");
        }
        const std::uint32_t func = state.top;
        state.set(func, tm);
        state.set(func + 1, v);
        state.top = func + 2;
        if (call(state, func, 1) != Status::Ok) {
            return Status::Error;
        }
        const Value r = state.get(func);
        state.top = func;
        if (r.is_string()) {
            out = r;
            return Status::Ok;
        }
        if (r.is_number()) {
            out = Value::string(heap.new_string(display_text(r)));
            return Status::Ok;
        }
        raise_error(state, "'__tostring' must return a string");
        return Status::Error;
    }
    if (v.is_string()) {
        out = v;
        return Status::Ok;
    }
    std::string text = display_text(v);
    if (v.is_table() || v.is_userdata()) {
        const Value name = metafield(heap, v, "__name");
        if (name.is_string()) {
            text = std::string{name.as_string()->view()} + text.substr(text.find(':'));
        }
    }
    out = Value::string(heap.new_string(text));
    return Status::Ok;
}

std::string error_text(State& state, Value error) {
    // lua.c's msghandler: a string or number as text; else __tostring's
    // result; else the type.
    if (error.is_string() || error.is_number()) {
        return display_text(error);
    }
    if (!metafield(state.heap(), error, "__tostring").is_nil()) {
        Value text;
        const std::uint32_t top = state.top;
        if (state.check_stack(3)) {
            state.push(error); // anchored while __tostring runs
            if (to_display_string(state, error, text) == Status::Ok && text.is_string()) {
                state.top = top;
                return std::string{text.as_string()->view()};
            }
        }
        state.top = top;
    }
    return std::format("(error object is a {} value)", type_name(error.type()));
}

// ---- Debug information ----

FunctionName function_name(const State& state, const CallInfo* frame) {
    // getfuncname and funcnamefromcall: a frame entered by a tail call has
    // lost its caller; a finalizer is "metamethod '__gc'"; otherwise the
    // calling Lua function's instruction tells.
    if (frame == nullptr || frame->tail_call || frame == &state.base_ci) {
        return {};
    }
    const CallInfo* caller = frame->previous;
    if (caller == nullptr) {
        return {};
    }
    if (caller->hooked) {
        return {.kind = "hook", .name = "?"};
    }
    if (caller->finalizer) {
        return {.kind = "metamethod", .name = "__gc"};
    }
    if (!state.is_lua(caller)) {
        return {};
    }
    const NameInfo found = called_name(proto_of(state, caller), frame_pc(state, caller));
    return {.kind = std::string{found.kind}, .name = std::string{found.name}};
}

std::optional<std::string> global_function_name(const State& state, Value function) {
    // pushglobalfuncname: findfield over the loaded modules, two levels
    // deep, "_G." dropped. Lua takes the first name its traversal finds,
    // so a function with two names (math.atan and math.atan2) gets either;
    // cpplua takes the shortest, then the first in order, every time.
    std::optional<std::string> best;
    const auto consider = [&best](std::string name) {
        if (name.starts_with("_G.")) {
            name.erase(0, 3);
        }
        if (!best || name.size() < best->size() || (name.size() == best->size() && name < *best)) {
            best = std::move(name);
        }
    };
    Table& loaded = state.heap().loaded();
    Value module_name;
    Value module;
    while (loaded.next_entry(module_name, module) == Table::NextResult::Found) {
        if (!module_name.is_string()) {
            continue;
        }
        if (module.payload() == function.payload() && module.tag() == function.tag()) {
            consider(std::string{module_name.as_string()->view()});
            continue;
        }
        if (!module.is_table()) {
            continue;
        }
        auto* table = std::bit_cast<Table*>(module.payload());
        Value field_name;
        Value field;
        while (table->next_entry(field_name, field) == Table::NextResult::Found) {
            if (field_name.is_string() && field.tag() == function.tag() &&
                field.payload() == function.payload()) {
                std::string name{module_name.as_string()->view()};
                name += '.';
                name += field_name.as_string()->view();
                consider(std::move(name));
            }
        }
    }
    return best;
}

std::string traceback(const State& state, std::optional<std::string_view> message, int level) {
    // luaL_traceback: the first 10 and last 11 levels of a deep stack.
    constexpr int levels1 = 10;
    constexpr int levels2 = 11;
    std::string out;
    if (message) {
        out += *message;
        out += '\n';
    }
    out += "stack traceback:";
    // lastlevel: the deepest level (counted once: a stack may hold a
    // million frames).
    int last = -1;
    for (const CallInfo* frame = state.ci; frame != nullptr && frame != &state.base_ci;
         frame = frame->previous) {
        ++last;
    }
    last = std::max(last, 0);
    int limit = last - level > levels1 + levels2 ? levels1 : -1;
    while (const CallInfo* frame = state.frame_at(level++)) {
        if (limit-- == 0) {
            const int n = last - level - levels2 + 1;
            out += std::format("\n\t...\t(skipping {} levels)", n);
            level += n;
            continue;
        }
        const FrameInfo info = frame_info(state, frame);
        if (info.current_line <= 0) {
            out += std::format("\n\t{}: in ", info.short_source);
        } else {
            out += std::format("\n\t{}:{}: in ", info.short_source, info.current_line);
        }
        // pushfuncname
        const FunctionName name = function_name(state, frame);
        if (const auto global = global_function_name(state, state.get(frame->func))) {
            out += std::format("function '{}'", *global);
        } else if (!name.kind.empty()) {
            out += std::format("{} '{}'", name.kind, name.name);
        } else if (info.is_main) {
            out += "main chunk";
        } else if (info.is_lua) {
            out += std::format("function <{}:{}>", info.short_source, info.line_defined);
        } else {
            out += '?';
        }
        if (frame->tail_call) {
            out += "\n\t(...tail calls...)";
        }
    }
    return out;
}

} // namespace cpplua
