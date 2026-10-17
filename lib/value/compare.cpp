#include "value/compare.hpp"

#include <cstring>
#include <string>
#include <string_view>

#include "value/locale.hpp"
#include "value/string.hpp"

namespace cpplua {

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

// l_strcmp: strings order byte by byte, as unsigned bytes (V8.4), in the
// "C" locale (std::string_view compares through std::char_traits<char>,
// which compares chars as unsigned char, like memcmp). In another locale
// they order as strcoll orders them (P4.1), piece by piece between the
// '\0's they may hold (Lua's strings, like cpplua's, end with one).
int compare_strings(Value a, Value b) {
    const std::string_view x = a.as_string()->view();
    const std::string_view y = b.as_string()->view();
    if (collation_is_bytes()) [[likely]] {
        return x.compare(y);
    }
    const char* s1 = x.data();
    const char* s2 = y.data();
    std::size_t rest1 = x.size();
    std::size_t rest2 = y.size();
    for (;;) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): the locale is the program's, as in Lua
        const int t = std::strcoll(s1, s2);
        if (t != 0) {
            return t;
        }
        std::size_t piece1 = std::strlen(s1);
        std::size_t piece2 = std::strlen(s2);
        if (piece2 == rest2) {
            return piece1 == rest1 ? 0 : 1; // y is finished: equal, or x is longer
        }
        if (piece1 == rest1) {
            return -1; // x is finished and y is not
        }
        ++piece1; // past the '\0'
        ++piece2;
        s1 += piece1; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        s2 += piece2; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        rest1 -= piece1;
        rest2 -= piece2;
    }
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
    case Tag::CClosure:
    case Tag::LightUserdata:
    case Tag::Userdata:
    case Tag::Thread:
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

} // namespace cpplua
