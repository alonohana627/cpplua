#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "value/value.hpp"
#include "vm/execute.hpp"
#include "vm/state.hpp"

namespace lua2 {

class Table;

// What C++ library functions use to read their arguments, return results
// and raise errors: the part of Lua's C API and lauxlib.c the library needs
// (docs/final_docs/stdlib.md). A function's arguments are in its frame,
// from slot ci->func + 1 up to the top; it pushes its results and returns
// how many, or c_function_error.

// How many arguments the running C++ function got (lua_gettop).
[[nodiscard]] int arg_count(const State& state);

// Argument n, counted from 1; nil past the last (lua_type is LUA_TNONE).
[[nodiscard]] Value arg(const State& state, int n);

[[nodiscard]] bool arg_is_none(const State& state, int n);

[[nodiscard]] bool arg_is_none_or_nil(const State& state, int n);

// The stack slot of argument n.
[[nodiscard]] std::uint32_t arg_slot(const State& state, int n);

// Pushes a result (the frame has min_stack free slots for them).
void push(State& state, Value v);

// ---- Errors (lauxlib) ----

// luaL_where: "chunk:line: " of the function at `level` (0 the running one),
// or "" when it is not a Lua function.
[[nodiscard]] std::string where(const State& state, int level);

// luaL_error: raises `message` with the position of the caller (level 1).
int raise_error(State& state, std::string_view message);

// luaL_argerror: "bad argument #n to 'name' (extra)".
int arg_error(State& state, int n, std::string_view extra);

// luaL_typeerror: "bad argument #n to 'name' (expected expected, got TYPE)".
int arg_type_error(State& state, int n, std::string_view expected);

// The checks of lauxlib: each returns false after raising the error, so a
// function goes `if (!check_...) return c_function_error;`.
[[nodiscard]] bool check_any(State& state, int n);                        // luaL_checkany
[[nodiscard]] bool check_table(State& state, int n, Table*& out);         // luaL_checktype
[[nodiscard]] bool check_integer(State& state, int n, std::int64_t& out); // luaL_checkinteger
[[nodiscard]] bool opt_integer(State& state, int n, std::int64_t fallback, std::int64_t& out);
[[nodiscard]] bool check_number(State& state, int n, double& out);      // luaL_checknumber
[[nodiscard]] bool check_string(State& state, int n, std::string& out); // luaL_checklstring

// ---- Conversions ----

// luaL_tolstring: the string tostring() and print show for v, through
// __tostring or __name (M5.1). Can fail (a failing __tostring).
Status to_display_string(State& state, Value v, Value& out);

// How a non-string error object is shown at the top level (lua.c's
// msghandler without the traceback): numbers as text, a __tostring
// result, or "(error object is a X value)".
std::string error_text(State& state, Value error);

// ---- Debug information (ldebug.c's lua_getinfo, for lauxlib) ----

// The name of the function at `level`, as its caller calls it: kind
// ("global", "local", "method", "field", "upvalue", "metamethod", "for
// iterator") and name; empty when unknown (getfuncname).
struct FunctionName {
    std::string kind;
    std::string name;
};

[[nodiscard]] FunctionName function_name(const State& state, const CallInfo* frame);

// The name a global function has in the loaded modules ("print",
// "debug.traceback"), if any (pushglobalfuncname).
[[nodiscard]] std::optional<std::string> global_function_name(const State& state, Value function);

// luaL_traceback: `message` (if any), then "stack traceback:" and a line per
// level from `level` on (R4).
[[nodiscard]] std::string traceback(const State& state, std::optional<std::string_view> message,
                                    int level);

} // namespace lua2
