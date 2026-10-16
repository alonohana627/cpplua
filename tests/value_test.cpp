#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
