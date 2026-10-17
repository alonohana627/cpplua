// The utf8 library: a port of Lua 5.4.9's lutf8lib.c
// (docs/final_docs/stdlib.md).

#include <array>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "vm/execute.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

constexpr std::uint32_t max_unicode = 0x10FFFFU;
constexpr std::uint32_t max_utf = 0x7FFFFFFFU;
constexpr std::string_view invalid_code = "invalid UTF-8 code"; // MSGInvalid

// The byte at i, or 0 past the end (a C string's '\0').
unsigned char byte_at(std::string_view s, std::size_t i) {
    return i < s.size() ? static_cast<unsigned char>(s[i]) : 0;
}

bool is_continuation(std::string_view s, std::size_t i) {
    return (byte_at(s, i) & 0xC0U) == 0x80U;
}

// u_posrelat: a position, negative from the end.
std::int64_t relative_position(std::int64_t pos, std::size_t len) {
    if (pos >= 0) {
        return pos;
    }
    if (0U - static_cast<std::uint64_t>(pos) > len) {
        return 0;
    }
    return static_cast<std::int64_t>(len) + pos + 1;
}

// utf8_decode: the code at s[i] and the index after it; npos when the
// sequence is invalid (or, if strict, a surrogate or past U+10FFFF).
constexpr std::size_t npos = std::string_view::npos;

std::size_t decode(std::string_view s, std::size_t i, std::uint32_t& value, bool strict) {
    static constexpr std::array<std::uint32_t, 6> limits{
        ~std::uint32_t{0}, 0x80, 0x800, 0x10000U, 0x200000U, 0x4000000U,
    };
    unsigned int c = byte_at(s, i);
    std::uint32_t res = 0;
    if (c < 0x80) {
        res = c;
    } else if (c >= 0xfe) {
        return npos; // would need six or more continuation bytes
    } else {
        std::size_t count = 0;
        for (; (c & 0x40U) != 0; c <<= 1U) {
            const unsigned int cc = byte_at(s, i + ++count);
            if ((cc & 0xC0U) != 0x80U) {
                return npos;
            }
            res = (res << 6U) | (cc & 0x3FU);
        }
        res |= static_cast<std::uint32_t>(c & 0x7FU) << (count * 5);
        if (res > max_utf || res < limits.at(count)) {
            return npos;
        }
        i += count;
    }
    if (strict && (res > max_unicode || (0xD800U <= res && res <= 0xDFFFU))) {
        return npos;
    }
    value = res;
    return i + 1;
}

int utf8_len(State& state) {
    std::string_view s;
    std::int64_t i = 0;
    std::int64_t j = 0;
    if (!check_view(state, 1, s) || !opt_integer(state, 2, 1, i) || !opt_integer(state, 3, -1, j)) {
        return c_function_error;
    }
    std::int64_t posi = relative_position(i, s.size());
    std::int64_t posj = relative_position(j, s.size());
    const bool lax = arg(state, 4).is_truthy();
    // luaL_argcheck(1 <= posi && --posi <= len): posi drops only when it is at least 1.
    if (posi < 1) {
        return arg_error(state, 2, "initial position out of bounds");
    }
    --posi;
    if (std::cmp_greater(posi, s.size())) {
        return arg_error(state, 2, "initial position out of bounds");
    }
    --posj;
    if (std::cmp_greater_equal(posj, s.size())) {
        return arg_error(state, 3, "final position out of bounds");
    }
    std::int64_t n = 0;
    while (posi <= posj) {
        std::uint32_t code = 0;
        const std::size_t next = decode(s, static_cast<std::size_t>(posi), code, !lax);
        if (next == npos) {
            push(state, Value::nil());
            push(state, Value::integer(posi + 1));
            return 2;
        }
        posi = static_cast<std::int64_t>(next);
        ++n;
    }
    push(state, Value::integer(n));
    return 1;
}

int utf8_codepoint(State& state) {
    std::string_view s;
    std::int64_t i = 0;
    if (!check_view(state, 1, s) || !opt_integer(state, 2, 1, i)) {
        return c_function_error;
    }
    const std::int64_t posi = relative_position(i, s.size());
    std::int64_t j = 0;
    if (!opt_integer(state, 3, posi, j)) {
        return c_function_error;
    }
    const std::int64_t pose = relative_position(j, s.size());
    const bool lax = arg(state, 4).is_truthy();
    if (posi < 1) {
        return arg_error(state, 2, "out of bounds");
    }
    if (std::cmp_greater(pose, s.size())) {
        return arg_error(state, 3, "out of bounds");
    }
    if (posi > pose) {
        return 0;
    }
    if (pose - posi >= INT_MAX) {
        return raise_error(state, "string slice too long");
    }
    if (!state.check_stack(static_cast<std::uint32_t>(pose - posi) + 1)) {
        return raise_error(state, "stack overflow (string slice too long)");
    }
    int n = 0;
    const auto end = static_cast<std::size_t>(pose);
    for (auto at = static_cast<std::size_t>(posi - 1); at < end;) {
        std::uint32_t code = 0;
        at = decode(s, at, code, !lax);
        if (at == npos) {
            return raise_error(state, invalid_code);
        }
        push(state, Value::integer(code));
        ++n;
    }
    return n;
}

// luaO_utf8esc: x in UTF-8, up to six bytes (x <= 0x7FFFFFFF).
void append_utf8(std::string& out, std::uint32_t x) {
    if (x < 0x80) {
        out += static_cast<char>(x);
        return;
    }
    std::array<char, 8> buff{};
    std::size_t n = 1;
    unsigned int mfb = 0x3f; // the maximum that fits in the first byte
    do {
        buff.at(buff.size() - n++) = static_cast<char>(0x80U | (x & 0x3fU));
        x >>= 6U;
        mfb >>= 1U;
    } while (x > mfb);
    buff.at(buff.size() - n) = static_cast<char>((~mfb << 1U) | x);
    out.append(buff.data() + buff.size() - n,
               n); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

int utf8_char(State& state) {
    const int n = arg_count(state);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        std::int64_t code = 0;
        if (!check_integer(state, i, code)) {
            return c_function_error;
        }
        if (code < 0 || std::cmp_greater(code, max_utf)) { // as an unsigned, code <= MAXUTF
            return arg_error(state, i, "value out of range");
        }
        append_utf8(out, static_cast<std::uint32_t>(code));
    }
    push_string(state, out);
    return 1;
}

int utf8_offset(State& state) {
    std::string_view s;
    std::int64_t n = 0;
    if (!check_view(state, 1, s) || !check_integer(state, 2, n)) {
        return c_function_error;
    }
    const auto len = static_cast<std::int64_t>(s.size());
    std::int64_t i = 0;
    if (!opt_integer(state, 3, n >= 0 ? 1 : len + 1, i)) {
        return c_function_error;
    }
    std::int64_t posi = relative_position(i, s.size());
    // luaL_argcheck(1 <= posi && --posi <= len): posi drops only when it is at least 1.
    if (posi < 1) {
        return arg_error(state, 3, "position out of bounds");
    }
    --posi;
    if (posi > len) {
        return arg_error(state, 3, "position out of bounds");
    }
    const auto cont = [&](std::int64_t p) {
        return is_continuation(s, static_cast<std::size_t>(p));
    };
    if (n == 0) {
        while (posi > 0 && cont(posi)) { // the start of this character
            --posi;
        }
    } else {
        if (cont(posi)) {
            return raise_error(state, "initial position is a continuation byte");
        }
        if (n < 0) {
            while (n < 0 && posi > 0) { // move back
                do {
                    --posi;
                } while (posi > 0 && cont(posi));
                ++n;
            }
        } else {
            --n; // the first character is where we are
            while (n > 0 && posi < len) {
                do {
                    ++posi;
                } while (cont(posi));
                --n;
            }
        }
    }
    push(state, n == 0 ? Value::integer(posi + 1) : Value::nil());
    return 1;
}

template <bool Strict>
int utf8_codes_step(State& state) {
    std::string_view s;
    if (!check_view(state, 1, s)) {
        return c_function_error;
    }
    const Value control = arg(state, 2);
    // (lua_Unsigned)lua_tointeger: floats and strings with an integer value
    // count too; anything else is 0.
    Value number = control;
    if (control.is_string()) {
        number = string_to_number(control.as_string()->view()).value_or(Value::nil());
    }
    auto n = static_cast<std::uint64_t>(to_integer(number).value_or(0));
    if (n < s.size()) {
        while (is_continuation(s, n)) { // the next character
            ++n;
        }
    }
    if (n >= s.size()) {
        return 0; // no more (also when n was negative)
    }
    std::uint32_t code = 0;
    const std::size_t next = decode(s, n, code, Strict);
    if (next == npos || is_continuation(s, next)) {
        return raise_error(state, invalid_code);
    }
    push(state, Value::integer(static_cast<std::int64_t>(n) + 1));
    push(state, Value::integer(code));
    return 2;
}

int utf8_codes(State& state) {
    const bool lax = arg(state, 2).is_truthy();
    std::string_view s;
    if (!check_view(state, 1, s)) {
        return c_function_error;
    }
    if (is_continuation(s, 0)) {
        return arg_error(state, 1, invalid_code);
    }
    push(state, Value::c_function(lax ? &utf8_codes_step<false> : &utf8_codes_step<true>));
    push(state, arg(state, 1));
    push(state, Value::integer(0));
    return 3;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array utf8_functions{
    LibraryFunction{.name = "offset", .function = &utf8_offset},
    LibraryFunction{.name = "codepoint", .function = &utf8_codepoint},
    LibraryFunction{.name = "char", .function = &utf8_char},
    LibraryFunction{.name = "len", .function = &utf8_len},
    LibraryFunction{.name = "codes", .function = &utf8_codes},
};

} // namespace

void open_utf8(Heap& heap) {
    Table* library = open_library(heap, "utf8", utf8_functions);
    // UTF8PATT: a character's bytes, "[\0-\x7F\xC2-\xFD][\x80-\xBF]*".
    constexpr std::string_view pattern{"[\0-\x7F\xC2-\xFD][\x80-\xBF]*", 14};
    library->set(heap, Value::string(heap.new_string("charpattern")),
                 Value::string(heap.new_string(pattern)));
}

} // namespace cpplua
