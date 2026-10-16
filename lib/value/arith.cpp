#include "value/arith.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace lua2 {

namespace {

// `attempt to perform arithmetic on a nil value` (V3.12). Like Lua's
// luaG_opinterror, it names the first operand if that is not a number, and
// the second otherwise.
RuntimeError arithmetic_error(Value a, Value b) {
    const Value culprit = a.is_number() ? b : a;
    std::string message = "attempt to perform arithmetic on a ";
    message += type_name(culprit.type());
    message += " value";
    return RuntimeError{std::move(message)};
}

// The float version of an operation (V3.4): both operands converted to
// floats, or the error when one is not a number.
template <class FloatOp>
ArithResult in_floats(Value a, Value b, FloatOp float_op) {
    const auto x = to_float(a);
    const auto y = to_float(b);
    if (!x || !y) {
        return std::unexpected(arithmetic_error(a, b));
    }
    return Value::number(float_op(*x, *y));
}

} // namespace

ArithResult add(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        return Value::integer(int_add(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, [](double x, double y) { return x + y; });
}

ArithResult subtract(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        return Value::integer(int_subtract(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, [](double x, double y) { return x - y; });
}

ArithResult multiply(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        return Value::integer(int_multiply(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, [](double x, double y) { return x * y; });
}

ArithResult divide(Value a, Value b) {
    return in_floats(a, b, [](double x, double y) { return x / y; });
}

ArithResult floor_divide(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        if (b.as_integer() == 0) {
            return std::unexpected(RuntimeError{"attempt to divide by zero"});
        }
        return Value::integer(int_floor_divide(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, float_floor_divide);
}

ArithResult modulo(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        if (b.as_integer() == 0) {
            return std::unexpected(RuntimeError{"attempt to perform 'n%0'"});
        }
        return Value::integer(int_modulo(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, float_modulo);
}

ArithResult power(Value a, Value b) {
    return in_floats(a, b, float_power);
}

ArithResult negate(Value a) {
    if (a.is_integer()) {
        return Value::integer(int_negate(a.as_integer()));
    }
    if (a.is_float()) {
        return Value::number(-a.as_float());
    }
    return std::unexpected(arithmetic_error(a, a));
}

double float_modulo(double a, double b) {
    double remainder = std::fmod(a, b);
    if ((remainder > 0) ? b < 0 : (remainder < 0 && b > 0)) {
        remainder += b;
    }
    return remainder;
}

double float_power(double a, double b) {
    return b == 2 ? a * a : std::pow(a, b);
}

} // namespace lua2
