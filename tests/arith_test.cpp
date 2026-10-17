#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "value/arith.hpp"
#include "value/value.hpp"
#include "value_printer.hpp"

using cpplua::Value;

namespace {

constexpr std::int64_t min_integer = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t max_integer = std::numeric_limits<std::int64_t>::max();
constexpr double inf = std::numeric_limits<double>::infinity();

Value i(std::int64_t v) {
    return Value::integer(v);
}

Value f(double v) {
    return Value::number(v);
}

// A result as text: "3" for an integer, "3.0" for a float, or "error: ..."
// with the message. So each check shows the subtype too.
std::string text(const cpplua::ArithResult& result) {
    if (!result) {
        return "error: " + result.error().message;
    }
    return cpplua::debug_text(*result);
}

bool is_nan(const cpplua::ArithResult& result) {
    return result && result->is_float() && std::isnan(result->as_float());
}

} // namespace

// ---- Part 2: arithmetic ----

TEST_CASE("V3.2 integer operands give an integer", "[value][arith]") {
    CHECK(text(cpplua::add(i(1), i(2))) == "3");
    CHECK(text(cpplua::subtract(i(5), i(7))) == "-2");
    CHECK(text(cpplua::multiply(i(3), i(-4))) == "-12");
    CHECK(text(cpplua::floor_divide(i(7), i(2))) == "3");
    CHECK(text(cpplua::modulo(i(7), i(3))) == "1");
    CHECK(text(cpplua::negate(i(5))) == "-5");
    CHECK(text(cpplua::negate(i(0))) == "0");
}

TEST_CASE("V3.3 / and ^ always give a float", "[value][arith]") {
    CHECK(text(cpplua::divide(i(4), i(2))) == "2.0");
    CHECK(text(cpplua::divide(i(1), i(2))) == "0.5");
    CHECK(text(cpplua::divide(i(-3), i(2))) == "-1.5");
    CHECK(text(cpplua::power(i(2), i(2))) == "4.0");
    CHECK(text(cpplua::power(i(2), i(10))) == "1024.0");
    CHECK(text(cpplua::power(i(2), i(-1))) == "0.5");
    CHECK(text(cpplua::power(i(0), i(0))) == "1.0");
}

TEST_CASE("V3.4 a float operand makes it a float operation", "[value][arith]") {
    CHECK(text(cpplua::add(i(1), f(2.0))) == "3.0");
    CHECK(text(cpplua::add(f(1.0), i(2))) == "3.0");
    CHECK(text(cpplua::subtract(f(0.5), i(1))) == "-0.5");
    CHECK(text(cpplua::multiply(f(2.5), i(2))) == "5.0");
    CHECK(text(cpplua::floor_divide(f(7.0), i(2))) == "3.0");
    CHECK(text(cpplua::modulo(f(7.5), i(2))) == "1.5");
    CHECK(text(cpplua::negate(f(2.0))) == "-2.0");
    CHECK(text(cpplua::negate(f(0.0))) == "-0.0");
    // Integers beyond 2^53 lose precision on the way to float.
    CHECK(text(cpplua::add(i(max_integer), f(0.0))) == "9223372036854775808.0");
}

TEST_CASE("V3.5 integer operations wrap around", "[value][arith]") {
    CHECK(text(cpplua::add(i(max_integer), i(1))) == std::to_string(min_integer));
    CHECK(text(cpplua::subtract(i(min_integer), i(1))) == std::to_string(max_integer));
    CHECK(text(cpplua::multiply(i(max_integer), i(2))) == "-2");
    CHECK(text(cpplua::multiply(i(min_integer), i(-1))) == std::to_string(min_integer));
    CHECK(text(cpplua::negate(i(min_integer))) == std::to_string(min_integer));
    STATIC_REQUIRE(cpplua::int_add(max_integer, 1) == min_integer);
    STATIC_REQUIRE(cpplua::int_negate(min_integer) == min_integer);
}

TEST_CASE("V3.6 integer // rounds toward minus infinity", "[value][arith]") {
    CHECK(text(cpplua::floor_divide(i(-7), i(2))) == "-4");
    CHECK(text(cpplua::floor_divide(i(7), i(-2))) == "-4");
    CHECK(text(cpplua::floor_divide(i(-7), i(-2))) == "3");
    CHECK(text(cpplua::floor_divide(i(6), i(3))) == "2");
    CHECK(text(cpplua::floor_divide(i(-6), i(3))) == "-2");
    CHECK(text(cpplua::floor_divide(i(0), i(-5))) == "0");
    CHECK(text(cpplua::floor_divide(i(min_integer), i(1))) == std::to_string(min_integer));
    // The one overflow: it wraps instead of crashing.
    CHECK(text(cpplua::floor_divide(i(min_integer), i(-1))) == std::to_string(min_integer));
    STATIC_REQUIRE(cpplua::int_floor_divide(-1, 2) == -1);
    STATIC_REQUIRE(cpplua::int_floor_divide(1, -2) == -1);
}

TEST_CASE("V3.7 integer % has the sign of the divisor", "[value][arith]") {
    CHECK(text(cpplua::modulo(i(5), i(3))) == "2");
    CHECK(text(cpplua::modulo(i(-5), i(3))) == "1");
    CHECK(text(cpplua::modulo(i(5), i(-3))) == "-1");
    CHECK(text(cpplua::modulo(i(-5), i(-3))) == "-2");
    CHECK(text(cpplua::modulo(i(6), i(-3))) == "0");
    CHECK(text(cpplua::modulo(i(-6), i(3))) == "0");
    CHECK(text(cpplua::modulo(i(7), i(-1))) == "0");
    CHECK(text(cpplua::modulo(i(min_integer), i(-1))) == "0");
    CHECK(text(cpplua::modulo(i(min_integer), i(max_integer))) == std::to_string(max_integer - 1));
    // a % b == a - (a // b) * b for every pair.
    for (std::int64_t a = -20; a <= 20; ++a) {
        for (std::int64_t b = -7; b <= 7; ++b) {
            if (b != 0) {
                CAPTURE(a, b);
                CHECK(cpplua::int_modulo(a, b) == a - (cpplua::int_floor_divide(a, b) * b));
            }
        }
    }
}

TEST_CASE("V3.8 integer // and % by zero are errors", "[value][arith]") {
    CHECK(text(cpplua::floor_divide(i(1), i(0))) == "error: attempt to divide by zero");
    CHECK(text(cpplua::floor_divide(i(0), i(0))) == "error: attempt to divide by zero");
    CHECK(text(cpplua::modulo(i(1), i(0))) == "error: attempt to perform 'n%0'");
    CHECK(text(cpplua::modulo(i(min_integer), i(0))) == "error: attempt to perform 'n%0'");
}

TEST_CASE("V3.9 float // and % floor, and dividing by zero is no error", "[value][arith]") {
    CHECK(text(cpplua::floor_divide(f(-7.0), i(2))) == "-4.0");
    CHECK(text(cpplua::floor_divide(f(7.5), f(-2.0))) == "-4.0");
    CHECK(text(cpplua::floor_divide(f(-0.0), i(1))) == "-0.0");
    CHECK(text(cpplua::floor_divide(f(0.5), i(-1))) == "-1.0");

    CHECK(text(cpplua::modulo(f(5.5), i(2))) == "1.5");
    CHECK(text(cpplua::modulo(f(-5.5), i(2))) == "0.5");
    CHECK(text(cpplua::modulo(f(5.5), i(-2))) == "-0.5");
    CHECK(text(cpplua::modulo(f(-5.5), i(-2))) == "-1.5");
    CHECK(text(cpplua::modulo(f(5.0), f(inf))) == "5.0");
    CHECK(text(cpplua::modulo(f(-5.0), f(inf))) == "inf");
    CHECK(text(cpplua::modulo(f(5.0), f(-inf))) == "-inf");
    CHECK(text(cpplua::modulo(f(-5.0), f(-inf))) == "-5.0");

    CHECK(text(cpplua::divide(i(1), i(0))) == "inf");
    CHECK(text(cpplua::divide(i(-1), i(0))) == "-inf");
    CHECK(is_nan(cpplua::divide(i(0), i(0))));
    CHECK(text(cpplua::floor_divide(i(1), f(0.0))) == "inf");
    CHECK(is_nan(cpplua::modulo(i(1), f(0.0))));
    CHECK(is_nan(cpplua::modulo(f(inf), i(2))));
}

TEST_CASE("V3.10 ^ is pow on floats", "[value][arith]") {
    CHECK(text(cpplua::power(i(2), f(0.5))) == "1.4142135623730951");
    CHECK(text(cpplua::power(f(-2.0), i(3))) == "-8.0");
    CHECK(text(cpplua::power(f(1.5), i(2))) == "2.25");
    CHECK(is_nan(cpplua::power(f(-8.0), f(1.0 / 3))));
    CHECK(text(cpplua::power(i(10), i(400))) == "inf");
}

TEST_CASE("V3.12 non-numbers are errors that name the culprit", "[value][arith]") {
    const auto message = [](std::string_view type) {
        return "error: attempt to perform arithmetic on a " + std::string{type} + " value";
    };
    CHECK(text(cpplua::add(Value::nil(), i(1))) == message("nil"));
    CHECK(text(cpplua::add(i(1), Value::nil())) == message("nil"));
    CHECK(text(cpplua::subtract(f(1.0), Value::boolean(true))) == message("boolean"));
    // Both wrong: the first is named.
    CHECK(text(cpplua::multiply(Value::boolean(false), Value::nil())) == message("boolean"));
    CHECK(text(cpplua::divide(Value::nil(), i(1))) == message("nil"));
    CHECK(text(cpplua::power(i(2), Value::nil())) == message("nil"));
    CHECK(text(cpplua::negate(Value::nil())) == message("nil"));
    // The type error comes before the division by zero.
    CHECK(text(cpplua::floor_divide(Value::nil(), i(0))) == message("nil"));
    CHECK(text(cpplua::modulo(i(1), Value::boolean(true))) == message("boolean"));
}
