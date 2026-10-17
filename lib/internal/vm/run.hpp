#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "value/value.hpp"
#include "vm/state.hpp"

namespace lua2 {

// Running chunks (E1): the steps between source text and results.

// Parses and compiles `source`, and pushes its main function onto the
// thread's stack as a closure whose one upvalue, _ENV, is the globals table
// (E1.2). Like Lua's lua_load: the error is the syntax error message.
// `chunk_name` is how messages name the chunk (Lua names a file chunk
// "@path" and shows "path").
std::expected<void, std::string> load(State& state, std::string_view source,
                                      std::string_view chunk_name);

// lua_load with Lua's own chunk name: "@path" for a file, "=name" for a
// name shown as it is, or else the text itself ([string "..."]); `mode` is
// load's: "b", "t" or "bt". lua2 loads no binary chunks.
std::expected<void, std::string> load_chunk(State& state, std::string_view source,
                                            std::string_view source_name, std::string_view mode);

// Reads a file to load as a chunk, without what luaL_loadfilex skips (a
// byte order mark, a first line starting with '#'): the error is Lua's
// "cannot open name: reason". An empty path reads stdin.
std::expected<std::string, std::string> read_chunk_file(const std::string& path);

// How run_text reports an error.
enum class ErrorReport : std::uint8_t {
    Message,   // the message only, as a .runerror file has it
    Traceback, // the message and a stack traceback, as `lua2 file.lua` prints it (R2.5)
};

// Loads and runs a chunk with no arguments, in protected mode, the way the
// standalone interpreter does (lua.c's docall: the chunk is called from a
// C++ function). Returns its results as `print` would show them (with
// __tostring, M5.1), or the error as text (R2.5). The stack is back to how
// it was afterwards.
std::expected<std::vector<std::string>, std::string>
run_text(State& state, std::string_view source, std::string_view chunk_name,
         ErrorReport report = ErrorReport::Message);

// Loads and runs a chunk in protected mode (E1.1). Returns its results, or
// the error as text. The values are not anchored anywhere: use them before
// the next collection can run (the next call into the VM).
std::expected<std::vector<Value>, std::string> run(State& state, std::string_view source,
                                                   std::string_view chunk_name);

// A value as `print` and `tostring` show it without metamethods
// (luaL_tolstring's default): numbers as V6 writes them, strings as they
// are, nil, true, false, and "function: ..." or "table: ..." with the
// address as the C library's %p writes it.
std::string display_text(Value value);

} // namespace lua2
