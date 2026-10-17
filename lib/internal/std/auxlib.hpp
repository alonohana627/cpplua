#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "value/value.hpp"
#include "vm/execute.hpp"
#include "vm/state.hpp"

namespace cpplua {

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
// luaL_checklstring without a copy: a number argument is converted in
// place (as lua_tolstring does), so the view lives as long as the call.
[[nodiscard]] bool check_view(State& state, int n, std::string_view& out);
[[nodiscard]] bool opt_view(State& state, int n, std::string_view fallback, std::string_view& out);
[[nodiscard]] bool opt_number(State& state, int n, double fallback, double& out);
// luaL_checkoption: the index of the argument in `options`.
[[nodiscard]] bool check_option(State& state, int n, std::string_view fallback,
                                std::span<const std::string_view> options, std::size_t& out);
// luaL_checktype for functions, and luaL_argexpected.
[[nodiscard]] bool check_function(State& state, int n);

// Pushes a new string.
void push_string(State& state, std::string_view s);

// A library: a new table of functions, made a global and a loaded module
// (luaL_newlib and luaL_requiref).
struct LibraryFunction {
    std::string_view name;
    CFunction function;
};

Table* open_library(Heap& heap, std::string_view name, std::span<const LibraryFunction> functions);
void set_functions(Heap& heap, Table& table, std::span<const LibraryFunction> functions);

// luaL_fileresult: true; or fail, the message ("name: " first when
// `name` is given) and the number of errno. Reads errno: call it right
// after the C library call.
int file_result(State& state, bool ok, const char* name);

// The failure of luaL_fileresult for the error number `en` (an errno
// value; 0 is "(no extra info)").
int error_result(State& state, int en, const char* name);

// luaL_execresult: what os.execute and a pipe's close return for `stat`.
int exec_result(State& state, int stat);

// A number formatted by the C library's printf (Lua's l_sprintf): MinGW's
// own C99 printf on MinGW, so NaN and %a come out as on other systems.
std::string c_format_number(const char* form, double value);

// ---- Tables and values ----

// The table object of a table value.
[[nodiscard]] Table* table_object(Value v);

// t[i] with __index (lua_geti), with the fast path for a present key.
Status get_index(State& state, Value t, std::int64_t i, Value& out);

// t[i] = v with __newindex (lua_seti), with the fast path for a present key.
Status set_index(State& state, Value t, std::int64_t i, Value v);

// #v with __len, which must give an integer (luaL_len).
Status length_integer(State& state, Value v, std::int64_t& out);

// ---- Conversions ----

// luaL_tolstring: the string tostring() and print show for v, through
// __tostring or __name (M5.1). Can fail (a failing __tostring).
Status to_display_string(State& state, Value v, Value& out);

// How a non-string error object is shown at the top level (lua.c's
// msghandler without the traceback): numbers as text, a __tostring
// result, or "(error object is a X value)".
std::string error_text(State& state, Value error);

// ---- Debug information (ldebug.c's lua_getinfo, for lauxlib) ----

// The prototype of a Lua frame's function, and the instruction the frame is
// at (currentpc).
[[nodiscard]] const Proto& frame_proto(const State& state, const CallInfo* frame);
[[nodiscard]] int frame_pc(const State& state, const CallInfo* frame);

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

} // namespace cpplua
