#pragma once

#include <string_view>

namespace cpplua {

// The banner `cpplua -v` prints, like `lua -v`'s: the version, the Lua
// version it implements, and the copyrights.
std::string_view version();

} // namespace cpplua
