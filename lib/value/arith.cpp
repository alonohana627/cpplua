#include "value/arith.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "value/convert.hpp"
#include "value/string.hpp"

namespace lua2 {

namespace {

// An operand as the string library's arithmetic sees it (lstrlib's tonum):
// a number as it is, a string converted (V5), anything else nothing.
std::optional<Value> string_library_operand(Value value) {
    if (value.is_number()) {
        return value;
    }
    if (value.is_string()) {
        return string_to_number(value.as_string()->view());
    }
    return std::nullopt;
}

// When an operand is not a number (V3.11, V3.12). If either is a string,
// Lua's string library takes over through its metamethods (lstrlib's arith):
// both operands converted, then the same operation again on the numbers, or
// `attempt to add a 'string' with a 'nil'` when one does not convert
// (trymt). `event` is the metamethod's name without "__". Otherwise it is
// the core's `attempt to perform arithmetic on a nil value`.
template <class Operation>
ArithResult not_numbers(std::string_view event, Value a, Value b, Operation operation) {
    if (!a.is_string() && !b.is_string()) {
        return std::unexpected(operand_error("perform arithmetic on", a, b));
    }
    const auto x = string_library_operand(a);
    const auto y = string_library_operand(b);
    if (x && y) {
        return operation(*x, *y);
    }
    std::string message = "attempt to ";
    message += event;
    message += " a '";
    message += type_name(a.type());
    message += "' with a '";
    message += type_name(b.type());
    message += '\'';
    return std::unexpected(RuntimeError{std::move(message)});
}

// The float version of an operation (V3.4) when both operands are numbers;
// otherwise not_numbers().
template <class FloatOp, class Operation>
ArithResult in_floats(Value a, Value b, FloatOp float_op, std::string_view event,
                      Operation operation) {
    const auto x = to_float(a);
    const auto y = to_float(b);
    if (!x || !y) {
        return not_numbers(event, a, b, operation);
    }
    return Value::number(float_op(*x, *y));
}

} // namespace

RuntimeError operand_error(std::string_view action, Value a, Value b) {
    const Value culprit = a.is_number() ? b : a;
    std::string message = "attempt to ";
    message += action;
    message += " a ";
    message += type_name(culprit.type());
    message += " value";
    return RuntimeError{std::move(message)};
}

ArithResult add(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        return Value::integer(int_add(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, [](double x, double y) { return x + y; }, "add", add);
}

ArithResult subtract(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        return Value::integer(int_subtract(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, [](double x, double y) { return x - y; }, "sub", subtract);
}

ArithResult multiply(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        return Value::integer(int_multiply(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, [](double x, double y) { return x * y; }, "mul", multiply);
}

ArithResult divide(Value a, Value b) {
    return in_floats(a, b, [](double x, double y) { return x / y; }, "div", divide);
}

ArithResult floor_divide(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        if (b.as_integer() == 0) {
            return std::unexpected(RuntimeError{"attempt to divide by zero"});
        }
        return Value::integer(int_floor_divide(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, float_floor_divide, "idiv", floor_divide);
}

ArithResult modulo(Value a, Value b) {
    if (a.is_integer() && b.is_integer()) {
        if (b.as_integer() == 0) {
            return std::unexpected(RuntimeError{"attempt to perform 'n%0'"});
        }
        return Value::integer(int_modulo(a.as_integer(), b.as_integer()));
    }
    return in_floats(a, b, float_modulo, "mod", modulo);
}

ArithResult power(Value a, Value b) {
    return in_floats(a, b, float_power, "pow", power);
}

ArithResult negate(Value a) {
    if (a.is_integer()) {
        return Value::integer(int_negate(a.as_integer()));
    }
    if (a.is_float()) {
        return Value::number(-a.as_float());
    }
    // A unary operator: Lua calls the metamethod with the operand twice.
    return not_numbers("unm", a, a, [](Value x, Value /*same*/) { return negate(x); });
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
