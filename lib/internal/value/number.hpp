#pragma once

#include <cstdint>
#include <optional>

#include "value/value.hpp"

namespace lua2 {

// Conversions between the two number subtypes (docs/spec/values.md, V2).

// Integer to float (V2.3): the nearest double. Exact up to 2^53 in size;
// beyond that the double is rounded (2^63 - 1 becomes 2^63).
constexpr double int_to_float(std::int64_t i) {
    return static_cast<double>(i);
}

// The largest whole double below `f` or equal to it. Like std::floor, but
// constexpr (libc++'s is not) and never a library call. From 2^52 up every
// double is whole, and inf and nan come back as they are.
constexpr double floor_float(double f) {
    constexpr double two_to_52 = 4503599627370496.0;
    // Written so that nan is not in range (every comparison with nan is
    // false): converting nan to an integer would be undefined behavior.
    const bool in_range = f > -two_to_52 && f < two_to_52;
    if (!in_range) {
        return f;
    }
    // Truncation is exact here, and rounds toward zero: one too high for
    // negative non-whole numbers.
    const auto truncated = static_cast<double>(static_cast<std::int64_t>(f));
    return truncated > f ? truncated - 1 : truncated;
}

// How float_to_int treats a float that is not a whole number (Lua's F2Imod).
enum class Rounding : std::uint8_t {
    Exact, // no conversion: 3.5 has no integer (V2.4)
    Floor, // round toward minus infinity: 3.5 becomes 3, -3.5 becomes -4
    Ceil,  // round toward plus infinity: 3.5 becomes 4, -3.5 becomes -3
};

// Float to integer (V2.4): the integer with the same value, or nothing when
// there is none. Outside the integer range [-2^63, 2^63), and for inf and nan,
// there is never one. Floor and Ceil round first; exact comparisons between
// integers and floats (part 4) and `for` loops need them. Like Lua's
// luaV_flttointns.
constexpr std::optional<std::int64_t> float_to_int(double f, Rounding rounding = Rounding::Exact) {
    double whole = floor_float(f);
    if (whole != f) { // not a whole number (nan also lands here)
        switch (rounding) {
        case Rounding::Exact: return std::nullopt;
        case Rounding::Floor: break;
        case Rounding::Ceil: whole += 1; break;
        }
    }
    // -2^63 is a valid integer; 2^63 is not. Both bounds are exact doubles,
    // and nan fails both comparisons.
    constexpr double two_to_63 = 9223372036854775808.0;
    if (whole >= -two_to_63 && whole < two_to_63) {
        return static_cast<std::int64_t>(whole);
    }
    return std::nullopt;
}

// A number as a float: an integer converted (V2.3), a float as it is.
// Nothing for values that are not numbers. (Strings join in part 8.)
constexpr std::optional<double> to_float(Value value) {
    if (value.is_integer()) {
        return int_to_float(value.as_integer());
    }
    if (value.is_float()) {
        return value.as_float();
    }
    return std::nullopt;
}

// A number as an integer: an integer as it is, a float converted with
// `rounding` (V2.4). Nothing for floats with no such integer, and for values
// that are not numbers. (Strings join in part 8.)
constexpr std::optional<std::int64_t> to_integer(Value value, Rounding rounding = Rounding::Exact) {
    if (value.is_integer()) {
        return value.as_integer();
    }
    if (value.is_float()) {
        return float_to_int(value.as_float(), rounding);
    }
    return std::nullopt;
}

} // namespace lua2
