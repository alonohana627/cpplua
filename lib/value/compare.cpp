#include "value/compare.hpp"

#include <string>
#include <string_view>

#include "value/string.hpp"

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

// Strings order byte by byte, as unsigned bytes (V8.4). std::string_view
// compares through std::char_traits<char>, which compares chars as unsigned
// char, like memcmp.
int compare_strings(Value a, Value b) {
    return a.as_string()->view().compare(b.as_string()->view());
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
    case Tag::Float:
        return a.as_float() == b.as_float(); // nan == nan is false
    // Interned: equal short strings are the same string.
    case Tag::ShortString: return a.as_string() == b.as_string();
    case Tag::LongString: return a.as_string()->view() == b.as_string()->view();
    case Tag::CFunction:
    case Tag::LuaClosure:
    case Tag::Table: return a.payload() == b.payload(); // by identity (V7.5)
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
    } else if (a.is_string() && b.is_string()) {
        return compare_strings(a, b) < 0;
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
    } else if (a.is_string() && b.is_string()) {
        return compare_strings(a, b) <= 0;
    }
    return std::unexpected(order_error(a, b));
}

} // namespace lua2
