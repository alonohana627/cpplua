#pragma once

#include <string>

#include "vm/proto.hpp"

namespace lua2 {

// What runtime error messages say about where a value came from (E8.2),
// found by looking at the bytecode the way Lua's ldebug.c does.

// The source line of instruction `pc` (luaG_getfuncline).
int line_of(const Proto& proto, int pc);

// The name of local variable number `n` (counted from 1) that is active at
// instruction `pc`, or nullptr (luaF_getlocalname).
const String* local_name(const Proto& proto, int n, int pc);

// " (local 'x')", " (global 'x')", " (upvalue 'x')", " (field 'x')",
// " (method 'x')", " (constant 'x')": where the value in register `reg` came
// from, as seen by instruction `pc`, or "" when the code does not tell
// (varinfo and getobjname).
std::string register_info(const Proto& proto, int pc, int reg);

// " (upvalue 'x')" for upvalue `index` (getupvalname).
std::string upvalue_info(const Proto& proto, int index);

// How the function called by instruction `pc` is named, for "attempt to
// call" messages: " (local 'f')", " (for iterator 'for iterator')", ...
// (funcnamefromcode), or "".
std::string called_info(const Proto& proto, int pc);

} // namespace lua2
