#include "value/convert.hpp"

#include <charconv>
#include <cmath>
#include <string_view>

#include "numeral.hpp"

namespace lua2 {

namespace {

// Lua's spaces (lisspace, isspace in the C locale): space, \t, \n, \v, \f,
// \r (V5.1).
constexpr std::string_view spaces = " \t\n\v\f\r";

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

std::optional<Value> string_to_number(std::string_view text) {
    // Spaces around the numeral are allowed (V5.1).
    const auto first = text.find_first_not_of(spaces);
    if (first == std::string_view::npos) {
        return std::nullopt; // empty, or only spaces
    }
    text = text.substr(first, text.find_last_not_of(spaces) - first + 1);

    // inf and nan are never numbers (V5.4): like Lua's l_str2d, anything
    // with an 'n' is refused, so strtod-style readers never see them.
    if (text.find_first_of("nN") != std::string_view::npos) {
        return std::nullopt;
    }

    // One sign (V5.2). The numeral itself starts right after it: "- 1" and
    // "--1" are not numbers.
    const bool negative = text.starts_with('-');
    if (negative || text.starts_with('+')) {
        text.remove_prefix(1);
    }
    if (text.empty() || text.front() == '-' || text.front() == '+') {
        return std::nullopt;
    }

    // An integer if it reads as one and fits (V5.3), otherwise a float
    // (V5.4); a '\0' anywhere makes both fail (V5.5).
    if (const auto integer = integer_numeral(text, negative)) {
        return Value::integer(*integer);
    }
    if (const auto number = float_numeral(text)) {
        return Value::number(negative ? -*number : *number);
    }
    return std::nullopt;
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
