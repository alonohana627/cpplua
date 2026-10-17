// The helpers of the standard library: a port of the parts of Lua 5.4's
// lauxlib.c (and of lapi.c and ldebug.c behind them) that the library needs.

#include "std/auxlib.hpp"

#include <format>
#include <string>

#include "gc/heap.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "vm/debug_info.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/proto.hpp"
#include "vm/run.hpp"

namespace lua2 {

namespace {

const Proto& proto_of(const State& state, const CallInfo* frame) {
    return *std::bit_cast<const LuaClosure*>(state.stack_values[frame->func])->proto;
}

int frame_pc(const State& state, const CallInfo* frame) {
    return static_cast<int>(frame->saved_pc - proto_of(state, frame).code.data()) - 1;
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
    // deep, "_G." dropped.
    Table& loaded = state.heap().loaded();
    Value module_name;
    Value module;
    while (loaded.next_entry(module_name, module) == Table::NextResult::Found) {
        if (!module_name.is_string()) {
            continue;
        }
        if (module.payload() == function.payload() && module.tag() == function.tag()) {
            return std::string{module_name.as_string()->view()};
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
                if (name.starts_with("_G.")) {
                    name.erase(0, 3);
                }
                return name;
            }
        }
    }
    return std::nullopt;
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
    int last = 0;
    while (state.frame_at(last + 1) != nullptr) {
        ++last;
    }
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

} // namespace lua2
