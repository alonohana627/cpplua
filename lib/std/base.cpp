// The base library so far: a port of Lua 5.4's lbaselib.c, the functions
// stages 6 to 8 need (docs/final_docs/stdlib.md). The rest of the library
// comes with stage 9.

#include "std/base.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "std/auxlib.hpp"
#include "value/compare.hpp"
#include "value/convert.hpp"
#include "value/table.hpp"
#include "vm/execute.hpp"
#include "vm/metamethods.hpp"
#include "vm/state.hpp"

namespace lua2 {

namespace {

// Every function has CFunction's signature: C++ functions may change the
// thread.
// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter)

int fail(State& state) {
    push(state, Value::nil()); // luaL_pushfail
    return 1;
}

// print(...): each argument as tostring shows it, separated by tabs, then
// a newline (luaB_print). Each is written as soon as it is converted.
int print(State& state) {
    const int n = arg_count(state);
    for (int i = 1; i <= n; ++i) {
        Value text;
        if (to_display_string(state, arg(state, i), text) != Status::Ok) {
            return c_function_error;
        }
        if (i > 1) {
            state.heap().write_output("\t");
        }
        state.heap().write_output(text.as_string()->view());
    }
    state.heap().write_output("\n");
    return 0;
}

int type(State& state) {
    if (arg_is_none(state, 1)) {
        return arg_error(state, 1, "value expected");
    }
    push(state, Value::string(state.heap().new_string(type_name(arg(state, 1).type()))));
    return 1;
}

int tostring(State& state) {
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    Value text;
    if (to_display_string(state, arg(state, 1), text) != Status::Ok) {
        return c_function_error;
    }
    push(state, text);
    return 1;
}

int tonumber(State& state) {
    if (arg_is_none_or_nil(state, 2)) { // a standard conversion
        const Value v = arg(state, 1);
        if (v.is_number()) {
            push(state, v);
            return 1;
        }
        if (v.is_string()) {
            if (const auto number = string_to_number(v.as_string()->view())) {
                push(state, *number);
                return 1;
            }
        } else if (!check_any(state, 1)) {
            return c_function_error;
        }
        return fail(state);
    }
    std::int64_t base = 0;
    if (!check_integer(state, 2, base)) {
        return c_function_error;
    }
    const Value v = arg(state, 1);
    if (!v.is_string()) {
        return arg_type_error(state, 1, "string");
    }
    if (base < 2 || base > 36) {
        return arg_error(state, 2, "base out of range");
    }
    // b_str2int
    const std::string_view s = v.as_string()->view();
    constexpr std::string_view spaces = " \f\n\r\t\v";
    std::size_t at = s.find_first_not_of(spaces);
    if (at == std::string_view::npos) {
        return fail(state);
    }
    bool negative = false;
    if (s[at] == '-') {
        negative = true;
        ++at;
    } else if (s[at] == '+') {
        ++at;
    }
    if (at >= s.size() || std::isalnum(static_cast<unsigned char>(s[at])) == 0) {
        return fail(state);
    }
    std::uint64_t n = 0;
    while (at < s.size() && std::isalnum(static_cast<unsigned char>(s[at])) != 0) {
        const auto c = static_cast<unsigned char>(s[at]);
        const int digit = std::isdigit(c) != 0 ? c - '0' : std::toupper(c) - 'A' + 10;
        if (digit >= base) {
            return fail(state);
        }
        n = (n * static_cast<std::uint64_t>(base)) + static_cast<std::uint64_t>(digit);
        ++at;
    }
    at = s.find_first_not_of(spaces, at);
    if (at != std::string_view::npos) {
        return fail(state);
    }
    push(state, Value::integer(static_cast<std::int64_t>(negative ? 0U - n : n)));
    return 1;
}

// ---- Raw access ----

int rawequal(State& state) {
    if (!check_any(state, 1) || !check_any(state, 2)) {
        return c_function_error;
    }
    push(state, Value::boolean(equals(arg(state, 1), arg(state, 2))));
    return 1;
}

int rawlen(State& state) {
    const Value v = arg(state, 1);
    if (v.is_table()) {
        push(state, Value::integer(
                        static_cast<std::int64_t>(std::bit_cast<Table*>(v.payload())->length())));
        return 1;
    }
    if (v.is_string()) {
        push(state, Value::integer(static_cast<std::int64_t>(v.as_string()->length())));
        return 1;
    }
    return arg_type_error(state, 1, "table or string");
}

int rawget(State& state) {
    Table* t = nullptr;
    if (!check_table(state, 1, t) || !check_any(state, 2)) {
        return c_function_error;
    }
    push(state, t->get(arg(state, 2)).value());
    return 1;
}

int rawset(State& state) {
    Table* t = nullptr;
    if (!check_table(state, 1, t) || !check_any(state, 2) || !check_any(state, 3)) {
        return c_function_error;
    }
    const Value key = arg(state, 2);
    if (const char* error = Table::check_key(key)) {
        return runtime_error(state, error) == Status::Error ? c_function_error : 0;
    }
    t->set(state.heap(), key, arg(state, 3));
    push(state, arg(state, 1));
    return 1;
}

// ---- Traversal ----

} // namespace

int base_next(State& state) {
    Table* t = nullptr;
    if (!check_table(state, 1, t)) {
        return c_function_error;
    }
    Value key = arg(state, 2);
    Value value;
    switch (t->next_entry(key, value)) {
    case Table::NextResult::Found:
        push(state, key);
        push(state, value);
        return 2;
    case Table::NextResult::End: push(state, Value::nil()); return 1;
    case Table::NextResult::InvalidKey: break;
    }
    runtime_error(state, "invalid key to 'next'");
    return c_function_error;
}

namespace {

int pairs(State& state) {
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    const Value t = arg(state, 1);
    const Value tm = metafield(state.heap(), t, "__pairs");
    if (tm.is_nil()) {
        push(state, Value::c_function(&base_next));
        push(state, t);
        push(state, Value::nil());
        return 3;
    }
    // __pairs(t), three results (M2.6).
    const std::uint32_t func = state.top;
    push(state, tm);
    push(state, t);
    if (call(state, func, 3) != Status::Ok) {
        return c_function_error;
    }
    return 3;
}

} // namespace

int base_ipairs_step(State& state) {
    std::int64_t i = 0;
    if (!check_integer(state, 2, i)) {
        return c_function_error;
    }
    i = static_cast<std::int64_t>(static_cast<std::uint64_t>(i) + 1U);
    push(state, Value::integer(i));
    Value v;
    if (index_value(state, arg(state, 1), Value::integer(i), v) != Status::Ok) {
        return c_function_error;
    }
    push(state, v);
    return v.is_nil() ? 1 : 2;
}

namespace {

int ipairs(State& state) {
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    push(state, Value::c_function(&base_ipairs_step));
    push(state, arg(state, 1));
    push(state, Value::integer(0));
    return 3;
}

int select(State& state) {
    const int n = arg_count(state);
    const Value first = arg(state, 1);
    if (first.is_string() && first.as_string()->view().starts_with('#')) {
        push(state, Value::integer(n - 1));
        return 1;
    }
    std::int64_t i = 0;
    if (!check_integer(state, 1, i)) {
        return c_function_error;
    }
    if (i < 0) {
        i = n + i;
    } else if (i > n) {
        i = n;
    }
    if (i < 1) {
        return arg_error(state, 1, "index out of range");
    }
    return n - static_cast<int>(i);
}

// ---- Metatables ----

int getmetatable(State& state) {
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    const Value v = arg(state, 1);
    Table* mt = metatable_of(state.heap(), v);
    if (mt == nullptr) {
        push(state, Value::nil());
        return 1;
    }
    const Value protected_mt = metafield(state.heap(), v, "__metatable");
    push(state, protected_mt.is_nil() ? Value::table(mt) : protected_mt);
    return 1;
}

int setmetatable(State& state) {
    const Value mt = arg(state, 2);
    Table* t = nullptr;
    if (!check_table(state, 1, t)) {
        return c_function_error;
    }
    if ((!mt.is_nil() && !mt.is_table()) || arg_is_none(state, 2)) {
        return arg_type_error(state, 2, "nil or table");
    }
    if (!metafield(state.heap(), arg(state, 1), "__metatable").is_nil()) {
        return raise_error(state, "cannot change a protected metatable");
    }
    state.heap().set_metatable(t, mt.is_nil() ? nullptr : std::bit_cast<Table*>(mt.payload()));
    push(state, arg(state, 1));
    return 1;
}

// ---- Errors ----

// luaB_error with the error object and level given.
int raise_at_level(State& state, Value v, std::int64_t level) {
    if (v.is_string() && level > 0) {
        const std::string position = where(state, static_cast<int>(level));
        if (!position.empty()) {
            v = Value::string(
                state.heap().new_string(position + std::string{v.as_string()->view()}));
        }
    }
    state.set_error(v);
    return c_function_error;
}

int error(State& state) {
    std::int64_t level = 1;
    if (!opt_integer(state, 2, 1, level)) {
        return c_function_error;
    }
    return raise_at_level(state, arg(state, 1), level);
}

int assert_value(State& state) {
    if (arg(state, 1).is_truthy()) {
        return arg_count(state); // all arguments
    }
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    const Value message = arg_is_none(state, 2)
                              ? Value::string(state.heap().new_string("assertion failed!"))
                              : arg(state, 2);
    return raise_at_level(state, message, 1);
}

int pcall(State& state) {
    // [f, args...] becomes [true, f, args...]; the call leaves its results
    // after the true, or the error object.
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    const std::uint32_t first = arg_slot(state, 1);
    for (std::uint32_t p = state.top; p > first; --p) {
        state.set(p, state.get(p - 1));
    }
    ++state.top;
    state.set(first, Value::boolean(true));
    if (protected_call(state, first + 1, all_results, Value::nil()) != Status::Ok) {
        state.set(first, Value::boolean(false)); // false, the error object
        return 2;
    }
    return static_cast<int>(state.top - first);
}

int xpcall(State& state) {
    // [f, h, args...] becomes [f, h, true, f, args...], h being the message
    // handler (R2.3).
    const Value handler = arg(state, 2);
    if (!handler.is_function()) {
        return arg_type_error(state, 2, "function");
    }
    const std::uint32_t first = arg_slot(state, 1);
    const std::uint32_t args = first + 2;
    for (std::uint32_t p = state.top + 1; p > args + 1; --p) {
        state.set(p, state.get(p - 2));
    }
    state.top += 2;
    state.set(args, Value::boolean(true));
    state.set(args + 1, state.get(first));
    if (protected_call(state, args + 1, all_results, handler) != Status::Ok) {
        state.set(args, Value::boolean(false));
        return 2;
    }
    return static_cast<int>(state.top - args);
}

// ---- The collector ----

int collectgarbage(State& state) {
    static constexpr std::array<std::string_view, 10> options{
        "stop",     "restart",    "collect",   "count",        "step",
        "setpause", "setstepmul", "isrunning", "generational", "incremental",
    };
    std::string option = "collect";
    if (!arg_is_none_or_nil(state, 1) && !check_string(state, 1, option)) {
        return c_function_error;
    }
    std::size_t o = 0;
    while (o < options.size() && options.at(o) != option) {
        ++o;
    }
    if (o == options.size()) {
        return arg_error(state, 1, "invalid option '" + option + "'");
    }
    Heap& heap = state.heap();
    if (heap.in_finalizer()) {
        return fail(state); // lua_gc refuses inside a finalizer
    }
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    const auto mode_name = [&](GcMode mode) {
        return Value::string(
            heap.new_string(mode == GcMode::Incremental ? "incremental" : "generational"));
    };
    const auto stored = [](int parameter) { return std::int64_t{parameter / 4} * 4; };
    GcParameters& p = heap.parameters();
    switch (o) {
    case 0:
        heap.stop();
        push(state, Value::integer(0));
        return 1;
    case 1:
        heap.restart();
        push(state, Value::integer(0));
        return 1;
    case 2:
        heap.collect();
        push(state, Value::integer(0));
        return 1;
    case 3: push(state, Value::number(static_cast<double>(heap.total_bytes()) / 1024.0)); return 1;
    case 4:
        if (!opt_integer(state, 2, 0, x)) {
            return c_function_error;
        }
        push(state, Value::boolean(heap.step_kilobytes(x)));
        return 1;
    case 5:
    case 6: {
        if (!opt_integer(state, 2, 0, x)) {
            return c_function_error;
        }
        int& parameter = o == 5 ? p.pause : p.step_multiplier;
        push(state, Value::integer(stored(parameter)));
        parameter = static_cast<int>(x);
        return 1;
    }
    case 7: push(state, Value::boolean(heap.is_running())); return 1;
    case 8: {
        if (!opt_integer(state, 2, 0, x) || !opt_integer(state, 3, 0, y)) {
            return c_function_error;
        }
        const Value previous = mode_name(heap.mode());
        if (x != 0) {
            p.minor_multiplier = static_cast<int>(x);
        }
        if (y != 0) {
            p.major_multiplier = static_cast<int>(y);
        }
        heap.set_mode(GcMode::Generational);
        push(state, previous);
        return 1;
    }
    default: {
        if (!opt_integer(state, 2, 0, x) || !opt_integer(state, 3, 0, y) ||
            !opt_integer(state, 4, 0, z)) {
            return c_function_error;
        }
        const Value previous = mode_name(heap.mode());
        if (x != 0) {
            p.pause = static_cast<int>(x);
        }
        if (y != 0) {
            p.step_multiplier = static_cast<int>(y);
        }
        if (z != 0) {
            p.step_size = static_cast<int>(z);
        }
        heap.set_mode(GcMode::Incremental);
        push(state, previous);
        return 1;
    }
    }
}

// ---- debug.traceback (the debug library comes with stage 9) ----

int traceback(State& state) {
    // db_traceback: a message that is not a string (or number) comes back
    // as it is.
    const Value message = arg(state, 1);
    if (!message.is_string() && !message.is_number() && !message.is_nil()) {
        push(state, message);
        return 1;
    }
    std::int64_t level = 1;
    if (!opt_integer(state, 2, 1, level)) {
        return c_function_error;
    }
    std::optional<std::string> text;
    if (!message.is_nil()) {
        std::string s;
        static_cast<void>(check_string(state, 1, s));
        text = s;
    }
    push(state, Value::string(state.heap().new_string(
                    lua2::traceback(state, text, static_cast<int>(level)))));
    return 1;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

void set_function(Heap& heap, Table& table, std::string_view name, CFunction f) {
    table.set(heap, Value::string(heap.new_string(name)), Value::c_function(f));
}

} // namespace

void open_base(Heap& heap) {
    // luaopen_base
    Table& g = heap.globals();
    set_function(heap, g, "assert", &assert_value);
    set_function(heap, g, "collectgarbage", &collectgarbage);
    set_function(heap, g, "error", &error);
    set_function(heap, g, "getmetatable", &getmetatable);
    set_function(heap, g, "ipairs", &ipairs);
    set_function(heap, g, "next", &base_next);
    set_function(heap, g, "pairs", &pairs);
    set_function(heap, g, "pcall", &pcall);
    set_function(heap, g, "print", &print);
    set_function(heap, g, "rawequal", &rawequal);
    set_function(heap, g, "rawget", &rawget);
    set_function(heap, g, "rawlen", &rawlen);
    set_function(heap, g, "rawset", &rawset);
    set_function(heap, g, "select", &select);
    set_function(heap, g, "setmetatable", &setmetatable);
    set_function(heap, g, "tonumber", &tonumber);
    set_function(heap, g, "tostring", &tostring);
    set_function(heap, g, "type", &type);
    set_function(heap, g, "xpcall", &xpcall);
    g.set(heap, Value::string(heap.new_string("_G")), Value::table(&g));
    g.set(heap, Value::string(heap.new_string("_VERSION")),
          Value::string(heap.new_string("Lua 5.4")));
}

void open_debug(Heap& heap) {
    // luaopen_debug, for now only traceback: a `debug` global and module.
    Table* debug = heap.new_table();
    heap.globals().set(heap, Value::string(heap.new_string("debug")), Value::table(debug));
    heap.loaded().set(heap, Value::string(heap.new_string("debug")), Value::table(debug));
    set_function(heap, *debug, "traceback", &traceback);
}

} // namespace lua2
