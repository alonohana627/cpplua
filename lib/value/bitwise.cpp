#include "value/bitwise.hpp"

#include <cstdint>

#include "value/number.hpp"

namespace lua2 {

namespace {

// The error for operands that do not convert (V4.2, V4.5), like Lua's
// luaT_trybinTM: an operand that is not a number comes first; when both are
// numbers, a float without an exact integer value is the problem.
RuntimeError bitwise_error(Value a, Value b) {
    if (!a.is_number() || !b.is_number()) {
        return operand_error("perform bitwise operation on", a, b);
    }
    return RuntimeError{"number has no integer representation"};
}

// Both operands as integers (V4.2), then `int_op` on them.
template <class IntOp>
ArithResult on_integers(Value a, Value b, IntOp int_op) {
    const auto x = to_integer(a);
    const auto y = to_integer(b);
    if (!x || !y) {
        return std::unexpected(bitwise_error(a, b));
    }
    return Value::integer(int_op(*x, *y));
}

} // namespace

ArithResult bit_and(Value a, Value b) {
    return on_integers(a, b, int_and);
}

ArithResult bit_or(Value a, Value b) {
    return on_integers(a, b, int_or);
}

ArithResult bit_xor(Value a, Value b) {
    return on_integers(a, b, int_xor);
}

ArithResult shift_left(Value a, Value b) {
    return on_integers(a, b, int_shift_left);
}

ArithResult shift_right(Value a, Value b) {
    return on_integers(a, b, int_shift_right);
}

ArithResult bit_not(Value a) {
    // A unary operator: Lua passes the operand twice (luaT_trybinTM).
    return on_integers(a, a, [](std::int64_t x, std::int64_t /*same*/) { return int_not(x); });
}

} // namespace lua2
