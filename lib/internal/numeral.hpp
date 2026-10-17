#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cpplua {

// Reading the value of a numeral (docs/spec/lexical.md, L5), shared by the
// lexer and by the conversion of strings to numbers (docs/spec/values.md,
// V5), as Lua shares luaO_str2num between them. The text is the numeral
// alone: no sign and no spaces around it.

constexpr bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// The value of a hex digit; only call it when is_hex_digit(c) is true.
constexpr unsigned hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a' + 10);
    }
    return static_cast<unsigned>(c - 'A' + 10);
}

// An integer numeral: decimal digits, or 0x and hex digits. Hex wraps around
// modulo 2^64 (L5.8, V5.3). A decimal that does not fit is not an integer
// (it reads as a float instead, L5.7). `negative` reads it as the integer
// after a minus sign, which lets -9223372036854775808 fit, like Lua's
// l_str2int. Nothing when the text is not exactly an integer numeral.
std::optional<std::int64_t> integer_numeral(std::string_view text, bool negative = false);

// A float numeral: decimal (1.5, .5, 5., 1e10) or hex (0x1.8p3). Too large or
// too small is not an error: 1e400 is inf and 1e-400 is 0 (L5.9). Nothing
// when the text is not exactly such a numeral.
std::optional<double> float_numeral(std::string_view text);

} // namespace cpplua
