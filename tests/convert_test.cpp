#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "value/convert.hpp"
#include "value/value.hpp"

using lua2::Value;

namespace {

std::string text(std::int64_t i) {
    return std::string{lua2::integer_text(i).view()};
}

std::string text(double f) {
    return std::string{lua2::float_text(f).view()};
}

// The number a string holds, shown with its subtype ("16", "16.0"), or
// "(none)".
std::string number(std::string_view s) {
    const auto value = lua2::string_to_number(s);
    return value ? lua2::debug_text(*value) : std::string{"(none)"};
}

} // namespace

// ---- Part 6: numbers to strings ----
// Many cases are strings.lua's, from the official test suite.

TEST_CASE("V6.1 integers are written in decimal", "[value][convert]") {
    CHECK(text(std::int64_t{0}) == "0");
    CHECK(text(std::int64_t{12}) == "12");
    CHECK(text(std::int64_t{-1203}) == "-1203");
    CHECK(text(std::int64_t{-32767}) == "-32767");
    CHECK(text(std::int64_t{-2147483647}) == "-2147483647");
    CHECK(text(std::int64_t{4611686018427387904}) == "4611686018427387904");
    CHECK(text(std::int64_t{-4611686018427387904}) == "-4611686018427387904");
    CHECK(text(std::int64_t{1234567890123}) == "1234567890123");
    CHECK(text(std::numeric_limits<std::int64_t>::max()) == "9223372036854775807");
    CHECK(text(std::numeric_limits<std::int64_t>::min()) == "-9223372036854775808");
}

TEST_CASE("V6.2 floats use %.14g and look like floats", "[value][convert]") {
    CHECK(text(0.0) == "0.0");
    CHECK(text(-0.0) == "-0.0");
    CHECK(text(1.0) == "1.0");
    CHECK(text(12.0) == "12.0");
    CHECK(text(-1203.0) == "-1203.0");
    CHECK(text(1203.125) == "1203.125");
    CHECK(text(-0.5) == "-0.5");
    CHECK(text(0.1) == "0.1");
    CHECK(text(100.0) == "100.0");
    // 14 significant digits: 0.1 + 0.2 shows as 0.3, pi is cut.
    CHECK(text(0.1 + 0.2) == "0.3");
    CHECK(text(3.141592653589793) == "3.1415926535898");
    // Exponents: from 1e14 on, and for small numbers. They contain 'e' and
    // '+' or '-', so no ".0" is added.
    CHECK(text(1e13) == "10000000000000.0");
    CHECK(text(1e14) == "1e+14");
    CHECK(text(1e15) == "1e+15");
    CHECK(text(1e100) == "1e+100");
    CHECK(text(-1e100) == "-1e+100");
    CHECK(text(9007199254740992.0) == "9.007199254741e+15");     // 2^53
    CHECK(text(9223372036854775808.0) == "9.2233720368548e+18"); // 2^63
    CHECK(text(0.0001) == "0.0001");
    CHECK(text(0.00001) == "1e-05");
    CHECK(text(5e-324) == "4.9406564584125e-324");
    CHECK(text(1.7976931348623157e308) == "1.7976931348623e+308");
}

TEST_CASE("V6.3 infinities and nan", "[value][convert]") {
    constexpr double inf = std::numeric_limits<double>::infinity();
    CHECK(text(inf) == "inf");
    CHECK(text(-inf) == "-inf");
    const double nan = std::nan("");
    CHECK(text(std::copysign(nan, 1.0)) == "nan");
    CHECK(text(std::copysign(nan, -1.0)) == "-nan");
}

TEST_CASE("V6.1 V6.2 the text of a number value", "[value][convert]") {
    const auto text_of = [](Value value) {
        const auto number = lua2::number_text(value);
        return number ? std::string{number->view()} : std::string{"(none)"};
    };
    CHECK(text_of(Value::integer(3)) == "3");
    CHECK(text_of(Value::number(3.0)) == "3.0");
    CHECK(text_of(Value::nil()) == "(none)");
    CHECK(text_of(Value::boolean(true)) == "(none)");
}

// ---- Part 7: strings to numbers ----
// Most cases are math.lua's `tonumber` tests, from the official test suite.

using namespace std::string_view_literals;

TEST_CASE("V5.1 the whole string, with spaces around it, is one numeral", "[value][convert]") {
    CHECK(number("0") == "0");
    CHECK(number(" 10 ") == "10");
    CHECK(number(" \t\n\v\f\r10\r\f\v\n\t ") == "10");
    CHECK(number(" 1.3e-2 ") == "0.013");
    CHECK(number("  +0.001e+3 \n\t") == "1.0");

    CHECK(number("") == "(none)");
    CHECK(number("  ") == "(none)");
    CHECK(number("10x") == "(none)");
    CHECK(number("1  a") == "(none)");
    CHECK(number("e1") == "(none)");
    CHECK(number("e  1") == "(none)");
    CHECK(number(" 3.4.5 ") == "(none)");
    CHECK(number("1e") == "(none)");
    CHECK(number("1.0e+") == "(none)");
    CHECK(number(".") == "(none)");
    CHECK(number("+.e1") == "(none)");
}

TEST_CASE("V5.2 one sign, right before the numeral", "[value][convert]") {
    CHECK(number("-012") == "-12");
    CHECK(number("-1.2e2") == "-120.0");
    CHECK(number("+0.01") == "0.01");
    CHECK(number("+.01") == "0.01");
    CHECK(number(".01") == "0.01");
    CHECK(number("-1.") == "-1.0");
    CHECK(number("+1.") == "1.0");
    CHECK(number("-0") == "0");
    CHECK(number("-0.0") == "-0.0");
    CHECK(number("+1.23E18") == "1.23e+18");

    CHECK(number("-") == "(none)");
    CHECK(number("+ 0.01") == "(none)");
    CHECK(number("--1") == "(none)");
    CHECK(number("+-1") == "(none)");
    CHECK(number("1-") == "(none)");
}

TEST_CASE("V5.3 integers, hex wrapping, and decimals too big", "[value][convert]") {
    CHECK(number("0xffffffffffff") == "281474976710655");
    CHECK(number("0xffffffffffffffff") == "-1");
    CHECK(number("-0xffffffffffffffff") == "1");
    CHECK(number("+0x2") == "2");
    CHECK(number("-0xaA") == "-170");
    CHECK(number("-0xffFFFfff") == "-4294967295");
    CHECK(number("9223372036854775807") == "9223372036854775807");
    CHECK(number("-9223372036854775808") == "-9223372036854775808");
    // Too big for an integer: a float.
    CHECK(number("9223372036854775808") == "9223372036854775808.0");
    CHECK(number("-9223372036854775809") == "-9223372036854775808.0");

    CHECK(number("0x") == "(none)");
    CHECK(number("  -0x ") == "(none)");
    CHECK(number("x") == "(none)");
    CHECK(number("x3") == "(none)");
    CHECK(number("00x2") == "(none)");
    CHECK(number("0x 2") == "(none)");
    CHECK(number("0 x2") == "(none)");
    CHECK(number("23x") == "(none)");
    CHECK(number("- 0xaa") == "(none)");
}

TEST_CASE("V5.4 floats, decimal and hex", "[value][convert]") {
    CHECK(number("  0x2.5  ") == "2.3125");
    CHECK(number("  -0x2.5  ") == "-2.3125");
    CHECK(number("  +0x0.51p+8  ") == "81.0");
    CHECK(number("0xA.a") == "10.625");
    CHECK(number("0x1.") == "1.0");
    CHECK(number("+0x.1") == "0.0625");
    CHECK(number("1e400") == "inf");
    CHECK(number("-1e400") == "-inf");
    CHECK(number(" -1.00000000000001 ") == "-1.00000000000001");

    CHECK(number("-0xaaP ") == "(none)");
    CHECK(number("0x0.51p") == "(none)");
    CHECK(number("0x5p+-2") == "(none)");
    CHECK(number("0x3.3.3") == "(none)");
}

TEST_CASE("V5.4 inf and nan are not numbers", "[value][convert]") {
    CHECK(number("inf") == "(none)");
    CHECK(number(" INF ") == "(none)");
    CHECK(number("-inf") == "(none)");
    CHECK(number("infinity") == "(none)");
    CHECK(number("Nan") == "(none)");
    CHECK(number("nan") == "(none)");
    CHECK(number("0xinf") == "(none)");
}

TEST_CASE("V5.5 a zero byte inside is never a number", "[value][convert]") {
    CHECK(number("1\0"sv) == "(none)");
    CHECK(number("1 \0"sv) == "(none)");
    CHECK(number("1\0 "sv) == "(none)");
    CHECK(number("\0"
                 "1"sv) == "(none)");
}

TEST_CASE("V5.6 the number keeps the subtype it read as", "[value][convert]") {
    CHECK(lua2::string_to_number("10").value_or(Value{}).is_integer());
    CHECK(lua2::string_to_number("10.0").value_or(Value{}).is_float());
    CHECK(lua2::string_to_number("1e1").value_or(Value{}).is_float());
    CHECK(lua2::string_to_number("0x10").value_or(Value{}).is_integer());
    CHECK(lua2::string_to_number("0x10p0").value_or(Value{}).is_float());
}

TEST_CASE("V5 numbers read back from their own text", "[value][convert]") {
    // tostring and then tonumber gives the same number (math.lua checks
    // this for maxint and minint), as far as 14 digits allow.
    constexpr std::array<std::int64_t, 5> integers{
        0,
        -1,
        1234567890123,
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::min(),
    };
    for (const std::int64_t i : integers) {
        CHECK(number(lua2::integer_text(i).view()) == text(i));
    }
    for (const double f : {0.0, -0.0, 0.5, 1e100, -1203.0, 1e-5}) {
        CHECK(number(lua2::float_text(f).view()) == text(f));
    }
}
