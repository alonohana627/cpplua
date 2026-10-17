#include "value/convert.hpp"

#include <charconv>
#include <cmath>
#include <string_view>

namespace lua2 {

namespace {

// The text of inf, -inf, nan and -nan (V6.3), the same on every platform:
// what glibc's printf writes. (On Windows, std::to_chars writes "-nan(ind)".)
std::string_view special_float_text(double f) {
    if (std::isnan(f)) {
        return std::signbit(f) ? "-nan" : "nan";
    }
    return f < 0 ? "-inf" : "inf";
}

} // namespace

NumberText integer_text(std::int64_t i) {
    NumberText text;
    char* const first = text.chars_.data();
    const auto result = std::to_chars(first, first + text.chars_.size(), i);
    text.length_ = static_cast<std::size_t>(result.ptr - first);
    return text;
}

NumberText float_text(double f) {
    NumberText text;
    if (!std::isfinite(f)) {
        const std::string_view special = special_float_text(f);
        special.copy(text.chars_.data(), special.size());
        text.length_ = special.size();
        return text;
    }
    // std::to_chars with chars_format::general and a precision is defined to
    // write what printf's %.14g writes, without printf's locale or varargs.
    constexpr int digits = 14; // Lua's LUAI_NUMFFORMAT, "%.14g"
    char* const first = text.chars_.data();
    char* const last = first + text.chars_.size();
    const auto result = std::to_chars(first, last, f, std::chars_format::general, digits);
    auto length = static_cast<std::size_t>(result.ptr - first);

    // "looks like an int?": only '-' and digits. Then add ".0", so a float
    // never reads back as an integer (Lua's tostringbuff).
    const std::string_view written{first, length};
    if (written.find_first_not_of("-0123456789") == std::string_view::npos) {
        text.chars_[length++] = '.';
        text.chars_[length++] = '0';
    }
    text.length_ = length;
    return text;
}

std::optional<NumberText> number_text(Value value) {
    if (value.is_integer()) {
        return integer_text(value.as_integer());
    }
    if (value.is_float()) {
        return float_text(value.as_float());
    }
    return std::nullopt;
}

} // namespace lua2
