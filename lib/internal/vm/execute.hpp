#pragma once

#include <cstdint>

#include "vm/state.hpp"

namespace lua2 {

// How a run of the VM ended.
enum class Status : std::uint8_t {
    Ok,
    Error, // the message is in State::error_message
};

// Calls the value in stack slot `func` with the values above it (up to
// thread.top) as arguments, like Lua's luaD_call: afterwards `wanted`
// results, or all of them for all_results, start at `func`, and thread.top is
// just past them (E6.2). Lua functions run in the VM without C++ recursion; this is
// only the entry from C++.
//
// On an error the message, with its position (E8.1), is in
// thread.error_message, and the stack and frames are left as they were at the
// error, for the caller to clean up (pcall comes with stage 8).
Status call(State& thread, std::uint32_t func, int wanted);

} // namespace lua2
