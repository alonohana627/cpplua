// The debug library: a port of Lua 5.4.9's ldblib.c, with the parts of
// ldebug.c's API it needs (lua_getinfo, lua_getlocal, lua_setlocal and the
// upvalue functions) (docs/final_docs/debug.md). Hooks run in the VM
// (vm/execute.cpp); debug.sethook keeps the hook function where Lua does,
// in the registry's _HOOKKEY table, weak in its keys (the threads).

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/debug_info.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/proto.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

Value string_value(State& state, std::string_view s) {
    return Value::string(state.heap().new_string(s));
}

// getthread: an optional thread first; `first` is then the index of the
// next argument.
State& thread_argument(State& state, int& first) {
    if (arg(state, 1).is_thread()) {
        first = 2;
        return *arg(state, 1).as_thread();
    }
    first = 1;
    return state;
}

// ---- lua_getinfo (ldebug.c) ----

// The function's prototype, for a Lua function.
const Proto* proto_of_function(Value f) {
    return f.is_closure() ? f.as_closure()->proto : nullptr;
}

// funcinfo: "S"
void source_info(State& state, Table& t, Value f) {
    Heap& heap = state.heap();
    const auto set = [&](std::string_view key, Value v) {
        t.set(heap, string_value(state, key), v);
    };
    const Proto* p = proto_of_function(f);
    if (p == nullptr) {
        set("source", string_value(state, "=[C]"));
        set("short_src", string_value(state, "[C]"));
        set("linedefined", Value::integer(-1));
        set("lastlinedefined", Value::integer(-1));
        set("what", string_value(state, "C"));
        return;
    }
    const std::string_view source = p->source != nullptr ? p->source->view() : "=?";
    set("source", string_value(state, source));
    set("short_src", string_value(state, chunk_id(source)));
    set("linedefined", Value::integer(p->line_defined));
    set("lastlinedefined", Value::integer(p->last_line_defined));
    set("what", string_value(state, p->line_defined == 0 ? "main" : "Lua"));
}

// collectvalidlines: "L"
Value active_lines(State& state, Value f) {
    const Proto* p = proto_of_function(f);
    if (p == nullptr) {
        return Value::nil();
    }
    Heap& heap = state.heap();
    Table* t = heap.new_table();
    // A vararg function's first instruction (VARARGPREP) is not a line.
    for (std::size_t i = p->is_vararg ? 1 : 0; i < p->lines.size(); ++i) {
        t->set_int(heap, p->lines[i], Value::boolean(true));
    }
    return Value::table(t);
}

// db_getinfo
int debug_getinfo(State& state) {
    int first = 0;
    State& thread = thread_argument(state, first);
    std::string_view options;
    if (!opt_view(state, first + 1, "flnSrtu", options)) {
        return c_function_error;
    }
    if (options.starts_with('>')) {
        return arg_error(state, first + 1, "invalid option '>'");
    }
    const Value target = arg(state, first);
    Value f;
    const CallInfo* ci = nullptr;
    if (target.is_function()) {
        f = target;
    } else {
        std::int64_t level = 0;
        if (!check_integer(state, first, level)) {
            return c_function_error;
        }
        ci = level < 0 || level > std::numeric_limits<int>::max()
                 ? nullptr
                 : thread.frame_at(static_cast<int>(level));
        if (ci == nullptr) {
            push(state, Value::nil()); // level out of range
            return 1;
        }
        f = thread.get(ci->func);
    }
    for (const char c : options) {
        if (!std::string_view{"SlutnrLf"}.contains(c)) {
            return arg_error(state, first + 1, "invalid option");
        }
    }
    Heap& heap = state.heap();
    Table* t = heap.new_table();
    push(state, Value::table(t)); // anchored
    const auto set = [&](std::string_view key, Value v) {
        t->set(heap, string_value(state, key), v);
    };
    const auto has = [&](char c) { return options.contains(c); };
    const Proto* p = proto_of_function(f);
    if (has('S')) {
        source_info(state, *t, f);
    }
    if (has('l')) {
        const bool lua = ci != nullptr && thread.is_lua(ci);
        set("currentline", Value::integer(lua ? line_of(*p, frame_pc(thread, ci)) : -1));
    }
    if (has('u')) {
        std::size_t upvalues = 0;
        if (f.is_closure()) {
            upvalues = f.as_closure()->upvalue_count();
        } else if (f.is_c_closure()) {
            upvalues = f.as_c_closure()->upvalue_count();
        }
        set("nups", Value::integer(static_cast<std::int64_t>(upvalues)));
        set("nparams", Value::integer(p != nullptr ? p->parameter_count : 0));
        set("isvararg", Value::boolean(p == nullptr || p->is_vararg));
    }
    if (has('n')) {
        const FunctionName name = function_name(thread, ci);
        set("name", name.kind.empty() ? Value::nil() : string_value(state, name.name));
        set("namewhat", string_value(state, name.kind));
    }
    if (has('r')) {
        const bool transfer = ci != nullptr && ci->transfer;
        set("ftransfer", Value::integer(transfer ? ci->first_transfer : 0));
        set("ntransfer", Value::integer(transfer ? ci->transfer_count : 0));
    }
    if (has('t')) {
        set("istailcall", Value::boolean(ci != nullptr && ci->tail_call));
    }
    if (has('L')) {
        set("activelines", active_lines(state, f));
    }
    if (has('f')) {
        set("func", f);
    }
    return 1;
}

// ---- Locals (luaG_findlocal) ----

// Where local `n` of frame `ci` is, and its name; nullopt when there is
// none. `n` < 0 is a vararg of a Lua vararg function.
struct LocalSlot {
    std::string name;
    std::uint32_t slot = 0;
};

std::optional<LocalSlot> find_local(const State& thread, const CallInfo* ci, int n) {
    const std::uint32_t base = ci->func + 1;
    std::string name;
    if (thread.is_lua(ci)) {
        const Proto& p = frame_proto(thread, ci);
        if (n < 0) { // findvararg
            if (p.is_vararg && n >= -ci->extra_args) {
                return LocalSlot{
                    .name = "(vararg)",
                    .slot = ci->func - static_cast<std::uint32_t>(ci->extra_args) -
                            static_cast<std::uint32_t>(n + 1),
                };
            }
            return std::nullopt;
        }
        if (const String* local = local_name(p, n, frame_pc(thread, ci))) {
            name = local->view();
        }
    }
    if (name.empty()) { // a slot without a name: a temporary
        const std::uint32_t limit = ci == thread.ci ? thread.top : ci->next->func;
        if (n > 0 && limit - base >= static_cast<std::uint32_t>(n)) {
            name = thread.is_lua(ci) ? "(temporary)" : "(C temporary)";
        } else {
            return std::nullopt;
        }
    }
    return LocalSlot{.name = name, .slot = base + static_cast<std::uint32_t>(n - 1)};
}

int debug_getlocal(State& state) {
    int first = 0;
    State& thread = thread_argument(state, first);
    std::int64_t n = 0;
    if (!check_integer(state, first + 1, n)) {
        return c_function_error;
    }
    const Value target = arg(state, first);
    if (target.is_function()) { // a parameter's name, at the function's start
        const Proto* p = proto_of_function(target);
        const String* name = p != nullptr ? local_name(*p, static_cast<int>(n), 0) : nullptr;
        push(state, name != nullptr ? Value::string(name) : Value::nil());
        return 1;
    }
    std::int64_t level = 0;
    if (!check_integer(state, first, level)) {
        return c_function_error;
    }
    const CallInfo* ci = level < 0 || level > std::numeric_limits<int>::max()
                             ? nullptr
                             : thread.frame_at(static_cast<int>(level));
    if (ci == nullptr) {
        return arg_error(state, first, "level out of range");
    }
    const auto local = find_local(thread, ci, static_cast<int>(n));
    if (!local) {
        push(state, Value::nil());
        return 1;
    }
    const Value value = thread.get(local->slot);
    push(state, string_value(state, local->name));
    push(state, value);
    return 2;
}

int debug_setlocal(State& state) {
    int first = 0;
    State& thread = thread_argument(state, first);
    std::int64_t level = 0;
    std::int64_t n = 0;
    if (!check_integer(state, first, level) || !check_integer(state, first + 1, n)) {
        return c_function_error;
    }
    const CallInfo* ci = level < 0 || level > std::numeric_limits<int>::max()
                             ? nullptr
                             : thread.frame_at(static_cast<int>(level));
    if (ci == nullptr) {
        return arg_error(state, first, "level out of range");
    }
    if (!check_any(state, first + 2)) {
        return c_function_error;
    }
    const auto local = find_local(thread, ci, static_cast<int>(n));
    if (!local) {
        push(state, Value::nil());
        return 1;
    }
    thread.set(local->slot, arg(state, first + 2));
    push(state, string_value(state, local->name));
    return 1;
}

// ---- Upvalues (lapi.c's aux_upvalue) ----

// Upvalue n of closure f: its name ("" for a C++ closure) and value.
struct UpvalueRef {
    std::string name;
    Value value;
};

std::optional<UpvalueRef> upvalue_of(Value f, std::int64_t n) {
    if (f.is_c_closure()) {
        const auto upvalues = f.as_c_closure()->upvalues();
        if (n < 1 || std::cmp_greater(n, upvalues.size())) {
            return std::nullopt;
        }
        return UpvalueRef{.name = "", .value = upvalues[static_cast<std::size_t>(n - 1)]};
    }
    if (f.is_closure()) {
        const LuaClosure* c = f.as_closure();
        const Proto* p = c->proto;
        if (n < 1 || std::cmp_greater(n, p->upvalues.size())) {
            return std::nullopt;
        }
        const String* name = p->upvalues[static_cast<std::size_t>(n - 1)].name;
        return UpvalueRef{
            .name = name != nullptr ? std::string{name->view()} : "(no name)",
            .value = c->upvalue(static_cast<std::size_t>(n - 1))->value(),
        };
    }
    return std::nullopt;
}

int debug_getupvalue(State& state) {
    std::int64_t n = 0;
    if (!check_integer(state, 2, n) || !check_function(state, 1)) {
        return c_function_error;
    }
    const auto upvalue = upvalue_of(arg(state, 1), n);
    if (!upvalue) {
        return 0;
    }
    push(state, string_value(state, upvalue->name));
    push(state, upvalue->value);
    return 2;
}

int debug_setupvalue(State& state) {
    std::int64_t n = 0;
    if (!check_any(state, 3) || !check_integer(state, 2, n) || !check_function(state, 1)) {
        return c_function_error;
    }
    const Value f = arg(state, 1);
    const auto upvalue = upvalue_of(f, n);
    if (!upvalue) {
        return 0;
    }
    const Value v = arg(state, 3);
    Heap& heap = state.heap();
    const auto index = static_cast<std::size_t>(n - 1);
    if (f.is_c_closure()) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): debug.setupvalue writes it
        auto* closure = const_cast<CClosure*>(f.as_c_closure());
        closure->upvalues()[index] = v;
        heap.barrier(closure, v);
    } else {
        Upvalue* upvalue_object = f.as_closure()->upvalue(index);
        upvalue_object->set(v);
        heap.barrier(upvalue_object, v);
    }
    push(state, string_value(state, upvalue->name));
    return 1;
}

// lua_upvalueid: the upvalue object of a Lua closure, the slot of a C++
// closure's; nullptr when there is none.
const void* upvalue_id(Value f, std::int64_t n) {
    if (f.is_closure()) {
        const LuaClosure* c = f.as_closure();
        if (n < 1 || std::cmp_greater(n, c->proto->upvalues.size())) {
            return nullptr;
        }
        return c->upvalue(static_cast<std::size_t>(n - 1));
    }
    if (f.is_c_closure()) {
        const auto upvalues = f.as_c_closure()->upvalues();
        if (n < 1 || std::cmp_greater(n, upvalues.size())) {
            return nullptr;
        }
        return &upvalues[static_cast<std::size_t>(n - 1)];
    }
    return nullptr;
}

// checkupval
bool check_upvalue(State& state, int function_arg, int index_arg, std::int64_t& n,
                   const void*& id) {
    if (!check_integer(state, index_arg, n) || !check_function(state, function_arg)) {
        return false;
    }
    id = upvalue_id(arg(state, function_arg), n);
    return true;
}

int debug_upvalueid(State& state) {
    std::int64_t n = 0;
    const void* id = nullptr;
    if (!check_upvalue(state, 1, 2, n, id)) {
        return c_function_error;
    }
    push(state, id != nullptr ? Value::light_userdata(id) : Value::nil());
    return 1;
}

int debug_upvaluejoin(State& state) {
    std::int64_t n1 = 0;
    std::int64_t n2 = 0;
    const void* id1 = nullptr;
    const void* id2 = nullptr;
    if (!check_upvalue(state, 1, 2, n1, id1)) {
        return c_function_error;
    }
    if (id1 == nullptr) {
        return arg_error(state, 2, "invalid upvalue index");
    }
    if (!check_upvalue(state, 3, 4, n2, id2)) {
        return c_function_error;
    }
    if (id2 == nullptr) {
        return arg_error(state, 4, "invalid upvalue index");
    }
    if (!arg(state, 1).is_closure()) {
        return arg_error(state, 1, "Lua function expected");
    }
    if (!arg(state, 3).is_closure()) {
        return arg_error(state, 3, "Lua function expected");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): debug.upvaluejoin writes it
    auto* f1 = const_cast<LuaClosure*>(arg(state, 1).as_closure());
    Upvalue* shared = arg(state, 3).as_closure()->upvalue(static_cast<std::size_t>(n2 - 1));
    f1->upvalues()[static_cast<std::size_t>(n1 - 1)] = shared;
    state.heap().object_barrier(f1, shared);
    return 0;
}

// ---- Metatables, user values, the registry ----

int debug_getregistry(State& state) {
    push(state, Value::table(&state.heap().registry()));
    return 1;
}

int debug_getmetatable(State& state) {
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    Table* mt = metatable_of(state.heap(), arg(state, 1));
    push(state, mt != nullptr ? Value::table(mt) : Value::nil());
    return 1;
}

int debug_setmetatable(State& state) {
    const Value mt = arg(state, 2);
    if (arg_is_none(state, 2) || (!mt.is_nil() && !mt.is_table())) { // (a nil, not none)
        return arg_type_error(state, 2, "nil or table");
    }
    Heap& heap = state.heap();
    Table* table = mt.is_nil() ? nullptr : table_object(mt);
    const Value v = arg(state, 1);
    if (v.is_table()) {
        heap.set_metatable(table_object(v), table);
    } else if (v.is_userdata()) {
        heap.set_metatable(v.as_userdata(), table);
    } else {
        heap.set_type_metatable(v.type(), table); // every value of the type
    }
    state.top = arg_slot(state, 1) + 1;
    return 1;
}

int debug_getuservalue(State& state) {
    std::int64_t n = 0;
    if (!opt_integer(state, 2, 1, n)) {
        return c_function_error;
    }
    const Value v = arg(state, 1);
    if (!v.is_userdata()) {
        push(state, Value::nil());
        return 1;
    }
    const auto values = v.as_userdata()->user_values();
    if (n < 1 || std::cmp_greater(n, values.size())) {
        push(state, Value::nil()); // LUA_TNONE: only the nil
        return 1;
    }
    push(state, values[static_cast<std::size_t>(n - 1)]);
    push(state, Value::boolean(true));
    return 2;
}

int debug_setuservalue(State& state) {
    std::int64_t n = 0;
    if (!opt_integer(state, 3, 1, n)) {
        return c_function_error;
    }
    if (!arg(state, 1).is_userdata()) {
        return arg_type_error(state, 1, "userdata");
    }
    if (!check_any(state, 2)) {
        return c_function_error;
    }
    Userdata* u = arg(state, 1).as_userdata();
    const auto values = u->user_values();
    if (n < 1 || std::cmp_greater(n, values.size())) {
        push(state, Value::nil());
        return 1;
    }
    values[static_cast<std::size_t>(n - 1)] = arg(state, 2);
    state.heap().barrier_back(u, arg(state, 2));
    push(state, arg(state, 1));
    return 1;
}

// ---- Hooks ----

constexpr std::string_view hook_key = "_HOOKKEY";

// The registry's table of hook functions by thread (made weak in its keys
// the first time).
Table& hook_table(State& state) {
    Heap& heap = state.heap();
    Table& registry = heap.registry();
    const Value key = string_value(state, hook_key);
    const Value found = registry.get(key).value();
    if (found.is_table()) {
        return *table_object(found);
    }
    Table* t = heap.new_table();
    registry.set(heap, key, Value::table(t));
    t->set(heap, string_value(state, "__mode"), string_value(state, "k"));
    heap.set_metatable(t, t);
    return *t;
}

int debug_sethook(State& state) {
    int first = 0;
    State& thread = thread_argument(state, first);
    std::uint8_t mask = 0;
    std::int64_t count = 0;
    Value function;
    if (!arg_is_none_or_nil(state, first)) {
        std::string_view smask;
        if (!check_view(state, first + 1, smask) || !check_function(state, first) ||
            !opt_integer(state, first + 2, 0, count)) {
            return c_function_error;
        }
        function = arg(state, first);
        if (smask.contains('c')) {
            mask |= hook_call;
        }
        if (smask.contains('r')) {
            mask |= hook_return;
        }
        if (smask.contains('l')) {
            mask |= hook_line;
        }
        count = static_cast<int>(count); // (int)luaL_optinteger, before the test, as Lua does
        if (count > 0) {
            mask |= hook_count;
        }
    }
    Table& hooks = hook_table(state);
    hooks.set(state.heap(), Value::thread(&thread), function);
    set_hook(thread, function.is_nil() ? 0 : mask, static_cast<int>(count));
    return 0;
}

int debug_gethook(State& state) {
    int first = 0;
    State& thread = thread_argument(state, first);
    if (thread.hook_mask == 0) {
        push(state, Value::nil());
        return 1;
    }
    push(state, hook_table(state).get(Value::thread(&thread)).value());
    std::string mask;
    if ((thread.hook_mask & hook_call) != 0) {
        mask += 'c';
    }
    if ((thread.hook_mask & hook_return) != 0) {
        mask += 'r';
    }
    if ((thread.hook_mask & hook_line) != 0) {
        mask += 'l';
    }
    push(state, string_value(state, mask));
    push(state, Value::integer(thread.base_hook_count));
    return 3;
}

// ---- The rest ----

int debug_traceback(State& state) {
    int first = 0;
    State& thread = thread_argument(state, first);
    const Value message = arg(state, first);
    if (!message.is_string() && !message.is_number() && !message.is_nil()) {
        push(state, message); // returned untouched
        return 1;
    }
    std::int64_t level = 0;
    if (!opt_integer(state, first + 1, &thread == &state ? 1 : 0, level)) {
        return c_function_error;
    }
    std::optional<std::string> text;
    if (!message.is_nil()) {
        std::string_view s;
        static_cast<void>(check_view(state, first, s));
        text = std::string{s};
    }
    push_string(state, traceback(thread, text, static_cast<int>(level)));
    return 1;
}

int debug_debug(State& state) {
    // db_debug: lines from stdin run until "cont".
    Heap& heap = state.heap();
    std::string line;
    for (;;) {
        heap.write_error("lua_debug> ");
        if (!std::getline(std::cin, line) || line == "cont") {
            return 0;
        }
        line += '\n';
        const std::uint32_t top = state.top;
        if (auto loaded = load_chunk(state, line, "=(debug command)", "bt"); !loaded) {
            heap.write_error(loaded.error() + "\n");
        } else if (protected_call(state, top, 0, Value::nil()) != Status::Ok) {
            Value text;
            if (to_display_string(state, state.get(top), text) == Status::Ok) {
                heap.write_error(std::string{text.as_string()->view()} + "\n");
            }
        }
        state.top = top;
    }
}

int debug_setcstacklimit(State& state) {
    // Deprecated in Lua 5.4: it changes nothing and gives the limit.
    std::int64_t limit = 0;
    if (!check_integer(state, 1, limit)) {
        return c_function_error;
    }
    push(state, Value::integer(max_c_calls));
    return 1;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array debug_functions{
    LibraryFunction{.name = "debug", .function = &debug_debug},
    LibraryFunction{.name = "getuservalue", .function = &debug_getuservalue},
    LibraryFunction{.name = "gethook", .function = &debug_gethook},
    LibraryFunction{.name = "getinfo", .function = &debug_getinfo},
    LibraryFunction{.name = "getlocal", .function = &debug_getlocal},
    LibraryFunction{.name = "getregistry", .function = &debug_getregistry},
    LibraryFunction{.name = "getmetatable", .function = &debug_getmetatable},
    LibraryFunction{.name = "getupvalue", .function = &debug_getupvalue},
    LibraryFunction{.name = "upvaluejoin", .function = &debug_upvaluejoin},
    LibraryFunction{.name = "upvalueid", .function = &debug_upvalueid},
    LibraryFunction{.name = "setuservalue", .function = &debug_setuservalue},
    LibraryFunction{.name = "sethook", .function = &debug_sethook},
    LibraryFunction{.name = "setlocal", .function = &debug_setlocal},
    LibraryFunction{.name = "setmetatable", .function = &debug_setmetatable},
    LibraryFunction{.name = "setupvalue", .function = &debug_setupvalue},
    LibraryFunction{.name = "traceback", .function = &debug_traceback},
    LibraryFunction{.name = "setcstacklimit", .function = &debug_setcstacklimit},
};

} // namespace

void open_debug(Heap& heap) {
    open_library(heap, "debug", debug_functions);
}

} // namespace cpplua
