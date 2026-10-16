#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "value/number.hpp"
#include "value/value.hpp"

using lua2::Tag;
using lua2::Type;
using lua2::Value;

// Catch2 prints values with debug_text when a check fails.
template <>
struct Catch::StringMaker<Value> {
    static std::string convert(Value value) { return lua2::debug_text(value); }
};

namespace {

constexpr std::int64_t min_integer = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t max_integer = std::numeric_limits<std::int64_t>::max();
constexpr double inf = std::numeric_limits<double>::infinity();

} // namespace

// ---- Part 0: foundations ----

TEST_CASE("V1.1 a value holds 8 bytes and a tag", "[value]") {
    STATIC_REQUIRE(sizeof(Value) == 16);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Value>);
    // Only the tag and the payload are needed to store and rebuild a value.
    const Value value = Value::number(2.5);
    CHECK(Value::from_parts(value.tag(), value.payload()).identical(value));
}

TEST_CASE("V1.2 type names are Lua's", "[value]") {
    STATIC_REQUIRE(lua2::type_name(Type::Nil) == "nil");
    STATIC_REQUIRE(lua2::type_name(Type::Boolean) == "boolean");
    STATIC_REQUIRE(lua2::type_name(Type::Number) == "number");
    STATIC_REQUIRE(lua2::type_name(Type::String) == "string");
    STATIC_REQUIRE(lua2::type_name(Type::Table) == "table");
    STATIC_REQUIRE(lua2::type_name(Type::Function) == "function");
    STATIC_REQUIRE(lua2::type_name(Type::Userdata) == "userdata");
    STATIC_REQUIRE(lua2::type_name(Type::Thread) == "thread");
}

TEST_CASE("V1.1 each value has its type", "[value]") {
    STATIC_REQUIRE(Value{}.type() == Type::Nil);
    STATIC_REQUIRE(Value::nil().type() == Type::Nil);
    STATIC_REQUIRE(Value::boolean(false).type() == Type::Boolean);
    STATIC_REQUIRE(Value::boolean(true).type() == Type::Boolean);
    STATIC_REQUIRE(Value::integer(0).type() == Type::Number);
    STATIC_REQUIRE(Value::number(0.0).type() == Type::Number);
}

TEST_CASE("V1.3 a number is an integer or a float, never both", "[value]") {
    STATIC_REQUIRE(Value::integer(1).is_integer());
    STATIC_REQUIRE_FALSE(Value::integer(1).is_float());
    STATIC_REQUIRE(Value::number(1.0).is_float());
    STATIC_REQUIRE_FALSE(Value::number(1.0).is_integer());
    STATIC_REQUIRE(Value::integer(1).is_number());
    STATIC_REQUIRE(Value::number(1.0).is_number());
    STATIC_REQUIRE_FALSE(Value::nil().is_number());
    STATIC_REQUIRE_FALSE(Value::boolean(true).is_number());

    // 1 and 1.0 are different values here; Lua's == (part 4) is what makes
    // them equal.
    CHECK_FALSE(Value::integer(1).identical(Value::number(1.0)));
}

TEST_CASE("V1.3 numbers keep every bit", "[value]") {
    const auto i = GENERATE(min_integer, std::int64_t{-1}, std::int64_t{0}, max_integer);
    CHECK(Value::integer(i).as_integer() == i);

    CHECK(Value::number(-0.0).as_float() == 0.0);
    CHECK(std::signbit(Value::number(-0.0).as_float()));
    CHECK(Value::number(inf).as_float() == inf);
    CHECK(std::isnan(Value::number(std::nan("")).as_float()));
    // Accessors work at compile time too.
    STATIC_REQUIRE(Value::integer(max_integer).as_integer() == max_integer);
    STATIC_REQUIRE(Value::number(0.5).as_float() == 0.5);
}

TEST_CASE("V1.4 only nil and false are falsy", "[value]") {
    STATIC_REQUIRE(Value::nil().is_falsy());
    STATIC_REQUIRE(Value::boolean(false).is_falsy());

    STATIC_REQUIRE(Value::boolean(true).is_truthy());
    STATIC_REQUIRE(Value::integer(0).is_truthy());
    STATIC_REQUIRE(Value::number(0.0).is_truthy());
    STATIC_REQUIRE(Value::number(-0.0).is_truthy());
    CHECK(Value::number(std::nan("")).is_truthy());
}

TEST_CASE("V1.5 nil has one value and booleans two", "[value]") {
    STATIC_REQUIRE(Value{}.identical(Value::nil()));
    STATIC_REQUIRE(Value::nil().is_nil());
    STATIC_REQUIRE(Value::boolean(true).as_boolean());
    STATIC_REQUIRE_FALSE(Value::boolean(false).as_boolean());
    STATIC_REQUIRE(Value::boolean(true).identical(Value::boolean(true)));
    STATIC_REQUIRE_FALSE(Value::boolean(true).identical(Value::boolean(false)));
    // Booleans carry no payload: the tag says it all.
    STATIC_REQUIRE(Value::boolean(true).tag() == Tag::True);
    STATIC_REQUIRE(Value::boolean(false).tag() == Tag::False);
}

TEST_CASE("debug_text shows each value and its subtype", "[value]") {
    CHECK(lua2::debug_text(Value::nil()) == "nil");
    CHECK(lua2::debug_text(Value::boolean(true)) == "true");
    CHECK(lua2::debug_text(Value::boolean(false)) == "false");
    CHECK(lua2::debug_text(Value::integer(-42)) == "-42");
    CHECK(lua2::debug_text(Value::integer(min_integer)) == "-9223372036854775808");
    CHECK(lua2::debug_text(Value::number(1.0)) == "1.0");
    CHECK(lua2::debug_text(Value::number(0.5)) == "0.5");
    CHECK(lua2::debug_text(Value::number(-inf)) == "-inf");
}

TEST_CASE("tag names", "[value]") {
    CHECK(lua2::tag_name(Tag::Nil) == "nil");
    CHECK(lua2::tag_name(Tag::False) == "false");
    CHECK(lua2::tag_name(Tag::True) == "true");
    CHECK(lua2::tag_name(Tag::Integer) == "integer");
    CHECK(lua2::tag_name(Tag::Float) == "float");
}

// ---- Part 1: integer and float conversions ----

using lua2::float_to_int;
using lua2::int_to_float;
using lua2::Rounding;

namespace {

constexpr double two_to_53 = 9007199254740992.0;
constexpr double two_to_63 = 9223372036854775808.0;
// The largest double below 2^63.
constexpr double below_two_to_63 = 9223372036854774784.0;

} // namespace

TEST_CASE("V2.1 integers are 64-bit two's complement", "[value]") {
    STATIC_REQUIRE(Value::integer(min_integer).as_integer() == -max_integer - 1);
    STATIC_REQUIRE(sizeof(Value::integer(0).as_integer()) == 8);
}

TEST_CASE("V2.3 integer to float is exact up to 2^53, rounded beyond", "[value]") {
    STATIC_REQUIRE(int_to_float(0) == 0.0);
    STATIC_REQUIRE(int_to_float(-3) == -3.0);
    STATIC_REQUIRE(int_to_float(9007199254740992) == two_to_53);
    // 2^53 + 1 has no double; it rounds to the even neighbor, 2^53.
    STATIC_REQUIRE(int_to_float(9007199254740993) == two_to_53);
    STATIC_REQUIRE(int_to_float(max_integer) == two_to_63);
    STATIC_REQUIRE(int_to_float(min_integer) == -two_to_63);
}

TEST_CASE("floor_float rounds toward minus infinity", "[value]") {
    STATIC_REQUIRE(lua2::floor_float(3.5) == 3.0);
    STATIC_REQUIRE(lua2::floor_float(-3.5) == -4.0);
    STATIC_REQUIRE(lua2::floor_float(-3.0) == -3.0);
    STATIC_REQUIRE(lua2::floor_float(0.25) == 0.0);
    STATIC_REQUIRE(lua2::floor_float(-0.25) == -1.0);
    STATIC_REQUIRE(lua2::floor_float(4503599627370495.5) == 4503599627370495.0);
    STATIC_REQUIRE(lua2::floor_float(-4503599627370495.5) == -4503599627370496.0);
    STATIC_REQUIRE(lua2::floor_float(1e300) == 1e300);
    STATIC_REQUIRE(lua2::floor_float(-inf) == -inf);
    CHECK(std::isnan(lua2::floor_float(std::nan(""))));
    // The same as std::floor on many values.
    for (int eighths = -8000; eighths <= 8000; ++eighths) {
        const double f = eighths * 0.125;
        CHECK(lua2::floor_float(f) == std::floor(f));
    }
}

TEST_CASE("V2.4 float to integer needs an exact integer value", "[value]") {
    STATIC_REQUIRE(float_to_int(3.0) == 3);
    STATIC_REQUIRE(float_to_int(-3.0) == -3);
    STATIC_REQUIRE(float_to_int(-0.0) == 0);
    STATIC_REQUIRE(float_to_int(two_to_53) == 9007199254740992);
    STATIC_REQUIRE(float_to_int(-two_to_63) == min_integer);
    STATIC_REQUIRE(float_to_int(below_two_to_63) == 9223372036854774784);

    STATIC_REQUIRE(float_to_int(3.5) == std::nullopt);
    STATIC_REQUIRE(float_to_int(-0.5) == std::nullopt);
    STATIC_REQUIRE(float_to_int(1e-300) == std::nullopt);
    // 2^63 is one past math.maxinteger.
    STATIC_REQUIRE(float_to_int(two_to_63) == std::nullopt);
    STATIC_REQUIRE(float_to_int(1e300) == std::nullopt);
    STATIC_REQUIRE(float_to_int(-1e300) == std::nullopt);
    STATIC_REQUIRE(float_to_int(inf) == std::nullopt);
    STATIC_REQUIRE(float_to_int(-inf) == std::nullopt);
    CHECK(float_to_int(std::nan("")) == std::nullopt);
}

TEST_CASE("V2.4 floor and ceil conversions round first", "[value]") {
    STATIC_REQUIRE(float_to_int(3.5, Rounding::Floor) == 3);
    STATIC_REQUIRE(float_to_int(-3.5, Rounding::Floor) == -4);
    STATIC_REQUIRE(float_to_int(3.5, Rounding::Ceil) == 4);
    STATIC_REQUIRE(float_to_int(-3.5, Rounding::Ceil) == -3);
    STATIC_REQUIRE(float_to_int(-0.5, Rounding::Ceil) == 0);
    STATIC_REQUIRE(float_to_int(0.5, Rounding::Floor) == 0);
    // Whole numbers are not moved.
    STATIC_REQUIRE(float_to_int(3.0, Rounding::Floor) == 3);
    STATIC_REQUIRE(float_to_int(3.0, Rounding::Ceil) == 3);
    // Rounding cannot bring a float into range.
    STATIC_REQUIRE(float_to_int(two_to_63, Rounding::Floor) == std::nullopt);
    STATIC_REQUIRE(float_to_int(-two_to_63 - 4096.0, Rounding::Ceil) == std::nullopt);
    STATIC_REQUIRE(float_to_int(inf, Rounding::Floor) == std::nullopt);
    STATIC_REQUIRE(float_to_int(-inf, Rounding::Ceil) == std::nullopt);
    CHECK(float_to_int(std::nan(""), Rounding::Floor) == std::nullopt);
    CHECK(float_to_int(std::nan(""), Rounding::Ceil) == std::nullopt);
}

TEST_CASE("V2.3 V2.4 numbers as floats or integers", "[value]") {
    STATIC_REQUIRE(lua2::to_float(Value::integer(2)) == 2.0);
    STATIC_REQUIRE(lua2::to_float(Value::number(2.5)) == 2.5);
    STATIC_REQUIRE(lua2::to_integer(Value::integer(2)) == 2);
    STATIC_REQUIRE(lua2::to_integer(Value::number(2.0)) == 2);
    STATIC_REQUIRE(lua2::to_integer(Value::number(2.5)) == std::nullopt);
    STATIC_REQUIRE(lua2::to_integer(Value::number(2.5), Rounding::Floor) == 2);

    // Values that are not numbers have no number.
    const auto other = GENERATE(Value::nil(), Value::boolean(false), Value::boolean(true));
    CHECK(lua2::to_float(other) == std::nullopt);
    CHECK(lua2::to_integer(other) == std::nullopt);
}
