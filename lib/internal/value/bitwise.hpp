#pragma once

#include <cstdint>

#include "value/arith.hpp"
#include "value/value.hpp"

namespace lua2 {

// Lua's bitwise operators (docs/spec/values.md, V4). Both operands are
// converted to integers, only when they are numbers with an exact integer
// value (V4.2); strings are never converted (V4.3). The result is always an
// integer.

ArithResult bit_and(Value a, Value b);     // a & b
ArithResult bit_or(Value a, Value b);      // a | b
ArithResult bit_xor(Value a, Value b);     // a ~ b
ArithResult shift_left(Value a, Value b);  // a << b
ArithResult shift_right(Value a, Value b); // a >> b
ArithResult bit_not(Value a);              // ~a

// ---- The operations on plain integers ----
//
// On the bits as a std::uint64_t, like Lua's intop: the same results as on
// std::int64_t in two's complement, with nothing implementation-defined.

constexpr std::int64_t int_and(std::int64_t x, std::int64_t y) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(x) & static_cast<std::uint64_t>(y));
}

constexpr std::int64_t int_or(std::int64_t x, std::int64_t y) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(x) | static_cast<std::uint64_t>(y));
}

constexpr std::int64_t int_xor(std::int64_t x, std::int64_t y) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(x) ^ static_cast<std::uint64_t>(y));
}

constexpr std::int64_t int_not(std::int64_t x) {
    return static_cast<std::int64_t>(~static_cast<std::uint64_t>(x));
}

// x << y. Shifts are logical (V4.4): vacated bits are zeros. A displacement
// of 64 or more either way gives 0, and a negative one shifts the other way.
// Like Lua's luaV_shiftl.
constexpr std::int64_t int_shift_left(std::int64_t x, std::int64_t y) {
    constexpr std::int64_t bits = 64;
    const auto ux = static_cast<std::uint64_t>(x);
    if (y < 0) { // a right shift
        if (y <= -bits) {
            return 0;
        }
        return static_cast<std::int64_t>(ux >> static_cast<std::uint64_t>(-y));
    }
    if (y >= bits) {
        return 0;
    }
    return static_cast<std::int64_t>(ux << static_cast<std::uint64_t>(y));
}

// x >> y is x << -y, with -y wrapping: -mininteger is mininteger, which is a
// right shift of 64 or more, so 0. Like Lua's luaV_shiftr.
constexpr std::int64_t int_shift_right(std::int64_t x, std::int64_t y) {
    return int_shift_left(x, int_negate(y));
}

} // namespace lua2
