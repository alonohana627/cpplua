#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vm/state.hpp"

namespace lua2 {

// What a C++ function returns to raise an error (lua_error): the error
// object is in State::error_value. Any other result is how many values it
// returns.
inline constexpr int c_function_error = -1;

// What a C++ function returns to yield (lua_yield), after yield_values():
// the thread stops, its frames as they are, until it is resumed (C1).
inline constexpr int c_function_yield = -2;

// LUAI_MAXCCALLS: calls through C++ (into the VM, metamethods, pcall) that
// may be nested (R3.2).
inline constexpr int max_c_calls = 200;

// Calls the value in stack slot `func` with the values above it (up to
// thread.top) as arguments, like Lua's luaD_call: afterwards `wanted`
// results, or all of them for all_results, start at `func`, and thread.top is
// just past them (E6.2). A value that is not a function is called through
// its __call metamethod (M4.1). Lua functions run in the VM without C++
// recursion; this is only the entry from C++.
//
// On an error the error object is in thread.error_value, and the stack and
// frames are left as they were where it happened (so a message handler can
// look at them); a protected call cleans up.
//
// The call may yield (Status::Yield, C2) when the running frame is Lua code
// (a metamethod called by the VM, as luaT_callTM decides); a call from a
// C++ function cannot: a yield inside is an error.
Status call(State& thread, std::uint32_t func, int wanted);

// A call that cannot yield (luaD_callnoyield).
Status call_no_yield(State& thread, std::uint32_t func, int wanted);

// lua_callk: a call from a C++ function that may yield; after the resume,
// `continuation` runs in place of the rest of the function (C3).
Status call_k(State& thread, std::uint32_t func, int wanted, Continuation continuation,
              std::intptr_t context);

// A protected call (luaD_pcall with lua_pcall's settings, R2): like call(),
// but an error stops here. If `handler` is not nil, it is called with the
// error object where the error happened, and its result becomes the error
// object (xpcall, R2.3); then the frames above are dropped, their upvalues
// and to-be-closed variables closed (R2.2), and the error object is left in
// slot `func`, with thread.top just past it.
Status protected_call(State& thread, std::uint32_t func, int wanted, Value handler);

// lua_pcallk: protected_call that may yield, then finishes in
// `continuation`, with Status::Error if an error ended the call after a
// resume (C3). pcall and xpcall use it.
Status protected_call_k(State& thread, std::uint32_t func, int wanted, Value handler,
                        Continuation continuation, std::intptr_t context);

// lua_sethook: thread's hooks watch the events in `mask` (hook_call and
// the others), a count hook every `count` instructions (H1). A mask of 0
// turns them off. The hook function itself is in the registry's _HOOKKEY
// table, as debug.sethook keeps it.
void set_hook(State& thread, std::uint8_t mask, int count);

// Stops the running Lua code at its next instruction with the error
// "interrupted!" (lua.c's Ctrl-C). Safe to call from a signal handler.
void request_interrupt();

// Whether an interrupt was requested, which this takes back (the VM's
// side of request_interrupt).
bool take_interrupt();

// ---- Coroutines (docs/spec/coroutines.md) ----

// lua_resume: runs coroutine `co` with the `nargs` values on its top as
// arguments (to its function, or as the results of the yield it stopped
// in), until it returns (Status::Ok: `results` values on its top), yields
// (Status::Yield: `results` values on its top) or fails (Status::Error: the
// coroutine is dead, the error in co.error_value). `from` is the running
// thread.
Status resume(State& co, const State& from, int nargs, int& results);

// lua_yield, for a C++ function: the thread stops, passing its top `n`
// values to the resume. Returns what the function must return:
// c_function_yield, or c_function_error when the thread cannot yield.
int yield_values(State& thread, int n);

// lua_closethread: closes the to-be-closed variables and upvalues of a
// suspended or dead coroutine, which becomes dead and empty. Status::Error
// when a __close fails or the coroutine had died of an error (in
// co.error_value).
Status close_thread(State& co, const State& from);

// Closes the upvalues and to-be-closed variables at stack slot `level` and
// above, as leaving their scope normally does (luaF_close with no error).
// Calls __close metamethods, so it can fail.
Status close_variables(State& thread, std::uint32_t level);

// Closes them in protected mode, as the end of the interpreter does
// (luaD_closeprotected): an error in one __close does not stop the others.
void close_variables_protected(State& thread, std::uint32_t level);

// luaG_runerror: raises `message`, with the position of the running
// function in front if it is a Lua function (E8.1). Always Status::Error.
Status runtime_error(State& thread, std::string_view message);

// Calls metamethod `f` with (p1, p2) and gives its first result (Lua's
// luaT_callTMres): the values are pushed at thread.top, which must be above
// every live slot.
Status call_metamethod(State& thread, Value f, Value p1, Value p2, Value& result);

// The value of t[k] with __index (luaV_finishget, M2.1), for C++ functions
// (lua_gettable). Like call_metamethod, it may push at thread.top.
Status index_value(State& thread, Value t, Value key, Value& result);

// t[key] = value with __newindex (luaV_finishset, M2.2), for C++ functions
// (lua_settable).
Status assign_value(State& thread, Value t, Value key, Value value);

// a < b, or a <= b, with __lt and __le (lua_compare with LUA_OPLT and
// LUA_OPLE, M3.5).
Status compare_values(State& thread, Value a, Value b, bool less_or_equal, bool& result);

// a == b with __eq (lua_compare with LUA_OPEQ, M3.4).
Status equal_values(State& thread, Value a, Value b, bool& result);

// #v with __len (lua_len, M3.6).
Status length_value(State& thread, Value v, Value& result);

} // namespace lua2
