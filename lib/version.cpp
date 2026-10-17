#include <cpplua/version.hpp>

namespace cpplua {

std::string_view version() {
    return "cpplua " CPPLUA_VERSION;
}

} // namespace cpplua
