#pragma once

#include <cstdint>
#include <expected>

#include "runtime_error.hpp"
#include "value/number.hpp"
#include "value/value.hpp"

namespace lua2 {

// Lua's comparison operators (docs/spec/values.md, V7 and V8). Strings join
// in part 5, metamethods in stage 8.

// a == b (V7). Values of different types are never equal; numbers are equal
// when their mathematical values are, whatever their subtypes, and nan is
// equal to nothing. It cannot fail. a ~= b is !equals(a, b). Like Lua's
// luaV_equalobj without metamethods.
bool equals(Value a, Value b);

using CompareResult = std::expected<bool, RuntimeError>;

// a < b and a <= b (V8). Numbers compare exactly by value; anything else is
// an error. a > b is less_than(b, a), and a >= b is less_equal(b, a): the
// operands swap, which is also the order the error message names them in.
CompareResult less_than(Value a, Value b);
CompareResult less_equal(Value a, Value b);

// ---- Exact comparisons between an integer and a float ----
//
// Converting the integer to float would round integers beyond 2^53, so
// math.maxinteger < 2^63 would come out false. Like Lua's LTintfloat and
// friends: integers within +-2^53 convert exactly, so those compare as
// floats; otherwise the float is rounded to an integer in the direction
// that keeps the answer the same (i < f exactly when i < ceil(f)), and
// floats beyond the integer range are above or below every integer. nan
// gives false everywhere.

// Whether i converts to float exactly: -2^53 <= i <= 2^53 (Lua's
// l_intfitsf).
constexpr bool fits_float(std::int64_t i) {
    constexpr std::int64_t two_to_53 = 9007199254740992;
    return -two_to_53 <= i && i <= two_to_53;
}

// i < f
constexpr bool int_less_float(std::int64_t i, double f) {
    if (fits_float(i)) {
        return int_to_float(i) < f;
    }
    if (const auto ceiling = float_to_int(f, Rounding::Ceil)) {
        return i < *ceiling;
    }
    return f > 0; // beyond every integer, or nan
}

// i <= f
constexpr bool int_less_equal_float(std::int64_t i, double f) {
    if (fits_float(i)) {
        return int_to_float(i) <= f;
    }
    if (const auto floor = float_to_int(f, Rounding::Floor)) {
        return i <= *floor;
    }
    return f > 0;
}

// f < i
constexpr bool float_less_int(double f, std::int64_t i) {
    if (fits_float(i)) {
        return f < int_to_float(i);
    }
    if (const auto floor = float_to_int(f, Rounding::Floor)) {
        return *floor < i;
    }
    return f < 0;
}

// f <= i
constexpr bool float_less_equal_int(double f, std::int64_t i) {
    if (fits_float(i)) {
        return f <= int_to_float(i);
    }
    if (const auto ceiling = float_to_int(f, Rounding::Ceil)) {
        return *ceiling <= i;
    }
    return f < 0;
}

} // namespace lua2
