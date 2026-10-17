#include <cstdint>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "value/arith.hpp"
#include "value/bitwise.hpp"
#include "value/value.hpp"
#include "value_printer.hpp"

using cpplua::Value;

namespace {

constexpr std::int64_t min_integer = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t max_integer = std::numeric_limits<std::int64_t>::max();

Value i(std::int64_t v) {
    return Value::integer(v);
}

Value f(double v) {
    return Value::number(v);
}

// The integer result, failing the test on an error or a non-integer.
std::int64_t ok(const cpplua::ArithResult& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_integer());
    return result->as_integer();
}

std::string error_of(const cpplua::ArithResult& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

// 0xFFFFFFFFFFFFFFFF and friends as Lua reads them: hex integers wrap.
constexpr std::int64_t hex(std::uint64_t bits) {
    return static_cast<std::int64_t>(bits);
}

} // namespace

// ---- Part 3: bitwise operations ----
// Most cases are bitwise.lua's, from the official test suite.

TEST_CASE("V4.1 the bitwise operators", "[value][bitwise]") {
    CHECK(ok(cpplua::bit_not(i(0))) == -1);
    CHECK(ok(cpplua::bit_and(i(0xF0), i(0xCC))) == 0xC0);
    CHECK(ok(cpplua::bit_or(i(0xF0), i(0xCC))) == 0xFC);
    CHECK(ok(cpplua::bit_xor(i(0xF0), i(0xCC))) == 0x3C);

    const Value a = i(hex(0xF0F0F0F0F0F0F0F0));
    CHECK(ok(cpplua::bit_or(a, i(-1))) == -1);
    CHECK(ok(cpplua::bit_xor(a, a)) == 0);
    CHECK(ok(cpplua::bit_xor(a, i(0))) == a.as_integer());
    CHECK(ok(cpplua::bit_xor(a, *cpplua::bit_not(a))) == -1);
    CHECK(ok(cpplua::shift_right(a, i(4))) == ok(cpplua::bit_not(a)));

    // a | b ~ c & d, which Lua reads as a | (b ~ (c & d)).
    const auto mix = [](Value a, Value b, Value c, Value d) {
        return ok(cpplua::bit_or(a, *cpplua::bit_xor(b, *cpplua::bit_and(c, d))));
    };
    CHECK(mix(i(0xF0), i(0xCC), i(0xAA), i(0xFD)) == 0xF4);
    CHECK(mix(i(0xF0000000), i(0xCC000000), i(0xAA000000), i(0xFD000000)) == 0xF4000000);
    CHECK(mix(i(hex(0xF0000000'00000000)), i(hex(0xCC000000'00000000)), i(hex(0xAA000000'00000000)),
              i(hex(0xFD000000'00000000))) == hex(0xF4000000'00000000));
}

TEST_CASE("V4.2 floats with an exact integer value convert", "[value][bitwise]") {
    CHECK(ok(cpplua::bit_or(f(3.0), i(0))) == 3);
    CHECK(ok(cpplua::bit_or(
              f(0xF0), *cpplua::bit_xor(f(0xCC), *cpplua::bit_and(f(0xAA), f(0xFD))))) == 0xF4);
    CHECK(ok(cpplua::bit_not(f(-1.0))) == 0);
    CHECK(ok(cpplua::bit_and(f(-0.0), i(-1))) == 0);
    // 2^30 - 1 shifted by 2^30 (a float) is 0.
    CHECK(ok(cpplua::shift_left(f(1073741823.0), f(1073741824.0))) == 0);
}

TEST_CASE("V4.2 a float without an integer value is an error", "[value][bitwise]") {
    const std::string message = "number has no integer representation";
    CHECK(error_of(cpplua::bit_or(f(3.5), i(0))) == message);
    CHECK(error_of(cpplua::bit_and(i(1), f(0.5))) == message);
    CHECK(error_of(cpplua::bit_xor(f(9223372036854775808.0), i(0))) == message); // 2^63
    CHECK(error_of(cpplua::shift_left(i(1), f(1e100))) == message);
    CHECK(error_of(cpplua::bit_not(f(1.5))) == message);
}

TEST_CASE("V4.3 strings are not converted", "[value][bitwise]") {
    // Strings come in part 5; until then this is the rule for every other
    // non-number, and V4.5 below covers the message.
    CHECK(error_of(cpplua::bit_and(Value::boolean(true), i(1))) ==
          "attempt to perform bitwise operation on a boolean value");
}

TEST_CASE("V4.4 shifts are logical and saturate at 64", "[value][bitwise]") {
    CHECK(ok(cpplua::shift_left(i(1), i(63))) == min_integer);
    CHECK(ok(cpplua::shift_left(i(1), i(31))) == 0x80000000);
    CHECK(ok(cpplua::shift_right(i(-1), i(1))) == max_integer);
    CHECK(ok(cpplua::shift_right(i(-1), i(63))) == 1);
    CHECK(ok(cpplua::shift_right(i(-1), i(64))) == 0);
    CHECK(ok(cpplua::shift_right(i(-1), i(-64))) == 0);
    CHECK(ok(cpplua::shift_left(i(-1), i(64))) == 0);
    CHECK(ok(cpplua::shift_left(i(-1), i(-64))) == 0);
    // A negative displacement shifts the other way.
    CHECK(ok(cpplua::shift_right(i(1), i(-3))) == 8);
    CHECK(ok(cpplua::shift_left(i(1000), i(-5))) == ok(cpplua::shift_right(i(1000), i(5))));
    CHECK(ok(cpplua::shift_left(i(1), i(-1))) == 0);
    CHECK(ok(cpplua::shift_right(i(2), i(-1))) == 4);
    // The extreme displacements.
    CHECK(ok(cpplua::shift_right(i(1), i(min_integer))) == 0);
    CHECK(ok(cpplua::shift_right(i(1), i(max_integer))) == 0);
    CHECK(ok(cpplua::shift_left(i(1), i(min_integer))) == 0);
    CHECK(ok(cpplua::shift_left(i(1), i(max_integer))) == 0);
    CHECK(ok(cpplua::shift_right(i(-1), i(min_integer))) == 0);
    STATIC_REQUIRE(cpplua::int_shift_right(-1, max_integer) == 0);
    STATIC_REQUIRE(cpplua::int_shift_left(-1, min_integer) == 0);
}

TEST_CASE("V4.4 ~ and - relate as in two's complement", "[value][bitwise]") {
    for (const std::int64_t x :
         {std::int64_t{0}, std::int64_t{0xFD000000}, min_integer, max_integer}) {
        CAPTURE(x);
        CHECK(ok(cpplua::bit_not(*cpplua::bit_not(i(x)))) == x);
        CHECK(ok(cpplua::bit_not(i(x))) == ok(cpplua::bit_xor(i(-1), i(x))));
        CHECK(ok(cpplua::negate(i(x))) == ok(cpplua::add(*cpplua::bit_not(i(x)), i(1))));
    }
}

TEST_CASE("V4.5 non-numbers are errors that name the culprit", "[value][bitwise]") {
    const auto message = [](std::string_view type) {
        return "attempt to perform bitwise operation on a " + std::string{type} + " value";
    };
    CHECK(error_of(cpplua::bit_and(Value::nil(), i(1))) == message("nil"));
    CHECK(error_of(cpplua::bit_or(i(1), Value::nil())) == message("nil"));
    CHECK(error_of(cpplua::bit_not(Value::nil())) == message("nil"));
    // Both wrong: the first is named.
    CHECK(error_of(cpplua::shift_left(Value::boolean(true), Value::nil())) == message("boolean"));
    // Not being a number comes before not being an integer.
    CHECK(error_of(cpplua::bit_xor(f(1.5), Value::nil())) == message("nil"));
}
