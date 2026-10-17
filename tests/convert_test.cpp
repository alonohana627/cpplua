#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

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
