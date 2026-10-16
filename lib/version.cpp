#include <lua2/version.hpp>

namespace lua2 {

std::string_view version() {
    return "lua2 " LUA2_VERSION;
}

} // namespace lua2
