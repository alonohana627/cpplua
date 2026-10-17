#include "value/locale.hpp"

#include <atomic>
#include <clocale>
#include <cstring>

namespace cpplua {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): the program's locale, cached
std::atomic<bool> bytes_order{true};
std::atomic<bool> c_ctype{true};
std::atomic<char> decimal_point{'.'};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

bool is_c_locale(const char* name) {
    return name == nullptr || std::strcmp(name, "C") == 0 || std::strcmp(name, "POSIX") == 0;
}

} // namespace

void refresh_locale() {
    // NOLINTBEGIN(concurrency-mt-unsafe): the locale is the program's, as in Lua
    bytes_order.store(is_c_locale(std::setlocale(LC_COLLATE, nullptr)), std::memory_order_relaxed);
    c_ctype.store(is_c_locale(std::setlocale(LC_CTYPE, nullptr)), std::memory_order_relaxed);
    const std::lconv* conventions = std::localeconv();
    const char point = conventions != nullptr && conventions->decimal_point != nullptr &&
                               conventions->decimal_point[0] != '\0'
                           ? conventions->decimal_point[0]
                           : '.';
    // NOLINTEND(concurrency-mt-unsafe)
    decimal_point.store(point, std::memory_order_relaxed);
}

bool collation_is_bytes() {
    return bytes_order.load(std::memory_order_relaxed);
}

bool ctype_is_c() {
    return c_ctype.load(std::memory_order_relaxed);
}

char locale_decimal_point() {
    return decimal_point.load(std::memory_order_relaxed);
}

} // namespace cpplua
