#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

#include "runtime_error.hpp"
#include "value/number.hpp"
#include "value/value.hpp"

namespace cpplua {

// Lua's arithmetic operators (docs/spec/values.md, V3). Each one works on
// two numbers: integers give an integer result (V3.2) except for / and ^,
// which always work in floats (V3.3); a float operand makes the operation a
// float one (V3.4). Strings that look like numbers take part, the way Lua's
// string library makes them (V3.11); anything else is an error (V3.12).

using ArithResult = std::expected<Value, RuntimeError>;

ArithResult add(Value a, Value b);          // a + b
ArithResult subtract(Value a, Value b);     // a - b
ArithResult multiply(Value a, Value b);     // a * b
ArithResult divide(Value a, Value b);       // a / b, always float
ArithResult floor_divide(Value a, Value b); // a // b
ArithResult modulo(Value a, Value b);       // a % b
ArithResult power(Value a, Value b);        // a ^ b, always float
ArithResult negate(Value a);                // -a

// `attempt to <action> a X value`, as in "attempt to perform arithmetic on a
// nil value" (V3.12, V4.5). Like Lua's luaG_opinterror, X is the type of the
// first operand if that is not a number, otherwise of the second. Unary
// operators pass their operand twice.
RuntimeError operand_error(std::string_view action, Value a, Value b);

// ---- The operations on plain numbers ----
//
// Integer operations wrap around modulo 2^64 (V3.5). C++ signed overflow is
// undefined behavior, so they compute on std::uint64_t, like Lua's intop.

constexpr std::int64_t int_add(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}

constexpr std::int64_t int_subtract(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}

constexpr std::int64_t int_multiply(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}

constexpr std::int64_t int_negate(std::int64_t a) {
    return int_subtract(0, a);
}

// a // b for b != 0 (V3.6): rounds toward minus infinity. C++ rounds toward
// zero, which is one too high when the signs differ and the division is not
// exact. b == -1 is negation, done apart because mininteger / -1 overflows
// in C++ (a crash on x86). Like Lua's luaV_idiv.
constexpr std::int64_t int_floor_divide(std::int64_t a, std::int64_t b) {
    if (b == -1) {
        return int_negate(a);
    }
    std::int64_t quotient = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) { // signs differ
        quotient -= 1;
    }
    return quotient;
}

// a % b for b != 0 (V3.7): the result has the sign of b. C++'s % has the sign
// of a, so a nonzero remainder with the other sign moves by b. x % -1 is 0,
// done apart because mininteger % -1 overflows in C++. Like Lua's luaV_mod.
constexpr std::int64_t int_modulo(std::int64_t a, std::int64_t b) {
    if (b == -1) {
        return 0;
    }
    std::int64_t remainder = a % b;
    if (remainder != 0 && ((remainder < 0) != (b < 0))) { // signs differ
        remainder += b;
    }
    return remainder;
}

// a // b on floats (V3.9): floor of the float division. Division by zero is
// not an error here: 1 // 0.0 is inf. Like Lua's luai_numidiv.
constexpr double float_floor_divide(double a, double b) {
    return floor_float(a / b);
}

// a % b on floats (V3.9), Lua's luai_nummod: fmod has the sign of a; when
// that differs from b's, move by b. So -5.5 % 2 is 0.5, and with an infinite
// b, -5 % inf is inf while 5 % inf is 5.
double float_modulo(double a, double b);

// a ^ b on floats (V3.10). Like Lua's luai_numpow, b == 2 is a * a.
double float_power(double a, double b);

} // namespace cpplua
