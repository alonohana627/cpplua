#include <cpplua/version.hpp>

namespace cpplua {

std::string_view version() {
    // As `lua -v` shows it (LUA_COPYRIGHT), saying it is cpplua.
    return "cpplua " CPPLUA_VERSION " (Lua 5.4.9)  Copyright (C) 2026 Alon Ohana; "
           "Lua Copyright (C) 1994-2026 Lua.org, PUC-Rio";
}

} // namespace cpplua
