#pragma once

#include <string>
#include <string_view>

namespace lua2 {

// Small formatting helpers shared by the debug views (token dump, AST
// printer).

// Bytes in double quotes, with anything unprintable escaped, so they fit on
// one line: "a\n\x01".
std::string quoted(std::string_view bytes);

// A float the way Lua prints it: the shortest text that reads back exactly,
// with ".0" added when it would look like an integer (1000.0, 0.5, inf).
std::string format_float(double value);

} // namespace lua2
