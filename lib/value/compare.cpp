#include "value/compare.hpp"

#include <string>
#include <string_view>

namespace lua2 {

namespace {

// `attempt to compare two nil values` or `attempt to compare number with nil`
// (V8.6), naming the operands in the order compared. Like Lua's
// luaG_ordererror.
RuntimeError order_error(Value a, Value b) {
    const std::string_view first = type_name(a.type());
    const std::string_view second = type_name(b.type());
    std::string message = "attempt to compare ";
    if (first == second) {
        message += "two ";
        message += first;
        message += " values";
    } else {
        message += first;
        message += " with ";
        message += second;
    }
    return RuntimeError{std::move(message)};
}

} // namespace

bool equals(Value a, Value b) {
    if (a.tag() != b.tag()) {
        // Only numbers of different subtypes can still be equal: an integer
        // and a float with the same integer value.
        if (!a.is_number() || !b.is_number()) {
            return false;
        }
        const auto x = to_integer(a);
        const auto y = to_integer(b);
        return x && y && *x == *y;
    }
    switch (a.tag()) {
    case Tag::Nil:
    case Tag::False:
    case Tag::True: return true;
    case Tag::Integer: return a.as_integer() == b.as_integer();
    case Tag::Float: return a.as_float() == b.as_float(); // nan == nan is false
    }
    return false; // not reached: every Tag is handled above
}

CompareResult less_than(Value a, Value b) {
    if (a.is_integer()) {
        if (b.is_integer()) {
            return a.as_integer() < b.as_integer();
        }
        if (b.is_float()) {
            return int_less_float(a.as_integer(), b.as_float());
        }
    } else if (a.is_float()) {
        if (b.is_float()) {
            return a.as_float() < b.as_float();
        }
        if (b.is_integer()) {
            return float_less_int(a.as_float(), b.as_integer());
        }
    }
    return std::unexpected(order_error(a, b));
}

CompareResult less_equal(Value a, Value b) {
    if (a.is_integer()) {
        if (b.is_integer()) {
            return a.as_integer() <= b.as_integer();
        }
        if (b.is_float()) {
            return int_less_equal_float(a.as_integer(), b.as_float());
        }
    } else if (a.is_float()) {
        if (b.is_float()) {
            return a.as_float() <= b.as_float();
        }
        if (b.is_integer()) {
            return float_less_equal_int(a.as_float(), b.as_integer());
        }
    }
    return std::unexpected(order_error(a, b));
}

} // namespace lua2
