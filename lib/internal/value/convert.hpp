#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "value/value.hpp"

namespace lua2 {

// Conversions between numbers and strings (docs/spec/values.md, V5 and V6).

// The text of a number, in a buffer on the stack: no allocation. 44 bytes is
// Lua's MAXNUMBER2STR, enough for any integer or %.14g float plus ".0".
class NumberText {
public:
    [[nodiscard]] std::string_view view() const { return {chars_.data(), length_}; }

private:
    friend NumberText integer_text(std::int64_t i);
    friend NumberText float_text(double f);

    std::array<char, 44> chars_{};
    std::size_t length_ = 0;
};

// An integer in decimal (V6.1): 10, -3, -9223372036854775808. Like Lua's
// lua_integer2str ("%lld").
NumberText integer_text(std::int64_t i);

// A float with 14 significant digits, as printf's %.14g writes it, plus ".0"
// when the result looks like an integer (V6.2): 1.0, -0.0, 0.1, 1e+15,
// 3.1415926535898. inf, -inf, nan and -nan are written that way (V6.3). Like
// Lua's tostringbuff.
NumberText float_text(double f);

// The text of a number value; nothing for other values.
std::optional<NumberText> number_text(Value value);

// The number a string holds (V5), as Lua's luaO_str2num reads it: one whole
// numeral with spaces around it and an optional sign, " -0x10 " or "1e2".
// An integer when it reads as an integer numeral that fits, otherwise a
// float; nothing when the string is not a number. inf and nan are never
// numbers here.
std::optional<Value> string_to_number(std::string_view text);

} // namespace lua2
