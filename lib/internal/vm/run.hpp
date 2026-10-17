#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "value/value.hpp"
#include "vm/state.hpp"

namespace lua2 {

// Running chunks (E1): the steps between source text and results.

// Parses and compiles `source`, and pushes its main function onto the thread's
// stack as a closure whose one upvalue, _ENV, is the globals table (E1.2). Like Lua's lua_load: the
// error is the syntax error message. `chunk_name` is how messages name the chunk (Lua names a file
// chunk
// "@path" and shows "path").
std::expected<void, std::string> load(State& state, std::string_view source,
                                      std::string_view chunk_name);

// Loads and calls a chunk with no arguments (E1.1). Returns its results, or
// the error message, after which the stack is back to how it was. The
// values are not anchored anywhere: use them before the next collection
// can run (the next call into the VM).
std::expected<std::vector<Value>, std::string> run(State& state, std::string_view source,
                                                   std::string_view chunk_name);

// A value as `print` and `tostring` show it (luaL_tolstring without
// metamethods): numbers as V6 writes them, strings as they are, nil, true,
// false, and "function: ..." or "table: ..." with the address as the C
// library's %p writes it.
std::string display_text(Value value);

} // namespace lua2
