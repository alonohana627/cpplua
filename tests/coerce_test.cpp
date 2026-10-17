#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/heap.hpp"
#include "value/arith.hpp"
#include "value/string.hpp"
#include "value/value.hpp"
#include "value_printer.hpp"

using lua2::Heap;
using lua2::Value;

namespace {

constexpr std::uint32_t test_seed = 12345;

// A result as text: "3" for an integer, "3.0" for a float, "\"ab\"" for a
// string, or "error: ..." with the message.
std::string text(const std::expected<Value, lua2::RuntimeError>& result) {
    if (!result) {
        return "error: " + result.error().message;
    }
    return lua2::debug_text(*result);
}

} // namespace

// ---- Part 8: coercions, concatenation and length ----

TEST_CASE("V3.11 strings that look like numbers take part in arithmetic", "[value][coerce]") {
    Heap heap{test_seed};
    const auto s = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    const auto i = [](std::int64_t v) { return Value::integer(v); };

    CHECK(text(lua2::add(s("10"), i(1))) == "11");
    CHECK(text(lua2::add(i(1), s("10"))) == "11");
    CHECK(text(lua2::add(s("3.0"), i(1))) == "4.0");
    CHECK(text(lua2::multiply(s("0x10"), i(1))) == "16");
    CHECK(text(lua2::subtract(s(" 7 "), s("2"))) == "5");
    CHECK(text(lua2::divide(s("1"), s("2"))) == "0.5");
    CHECK(text(lua2::power(s("2"), i(10))) == "1024.0");
    CHECK(text(lua2::modulo(s("-5"), i(3))) == "1");
    CHECK(text(lua2::floor_divide(s("7"), s("2"))) == "3");
    CHECK(text(lua2::negate(s("2"))) == "-2");
    CHECK(text(lua2::negate(s("-0.0"))) == "0.0");
    // math.lua: '0xA.a' + 0 and '1.' + '.1'.
    CHECK(text(lua2::add(s("0xA.a"), i(0))) == "10.625");
    CHECK(text(lua2::add(s("1."), s(".1"))) == "1.1");
    // The converted numbers follow every number rule, errors included.
    CHECK(text(lua2::floor_divide(s("1"), s("0"))) == "error: attempt to divide by zero");
    CHECK(text(lua2::modulo(i(1), s("0"))) == "error: attempt to perform 'n%0'");
    CHECK(text(lua2::add(s("9223372036854775807"), i(1))) == "-9223372036854775808");
}

TEST_CASE("V3.11 when a string does not convert, the string library complains", "[value][coerce]") {
    Heap heap{test_seed};
    const auto s = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };

    CHECK(text(lua2::add(s("abc"), Value::integer(1))) ==
          "error: attempt to add a 'string' with a 'number'");
    CHECK(text(lua2::add(Value::integer(1), s("abc"))) ==
          "error: attempt to add a 'number' with a 'string'");
    CHECK(text(lua2::subtract(s("10"), Value::nil())) ==
          "error: attempt to sub a 'string' with a 'nil'");
    CHECK(text(lua2::multiply(Value::nil(), s("10"))) ==
          "error: attempt to mul a 'nil' with a 'string'");
    CHECK(text(lua2::divide(s("x"), s("y"))) == "error: attempt to div a 'string' with a 'string'");
    CHECK(text(lua2::modulo(s("x"), Value::boolean(true))) ==
          "error: attempt to mod a 'string' with a 'boolean'");
    CHECK(text(lua2::power(s("x"), Value::integer(2))) ==
          "error: attempt to pow a 'string' with a 'number'");
    CHECK(text(lua2::floor_divide(s("x"), Value::integer(2))) ==
          "error: attempt to idiv a 'string' with a 'number'");
    CHECK(text(lua2::negate(s("x"))) == "error: attempt to unm a 'string' with a 'string'");
    CHECK(text(lua2::add(s("inf"), Value::integer(1))) ==
          "error: attempt to add a 'string' with a 'number'");

    // Without a string, it is the core's message (V3.12).
    CHECK(text(lua2::add(Value::nil(), Value::integer(1))) ==
          "error: attempt to perform arithmetic on a nil value");
}

TEST_CASE("V9.1 .. joins strings and numbers", "[value][coerce]") {
    Heap heap{test_seed};
    const auto s = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    const auto join = [&](std::vector<Value> values) {
        return text(lua2::concatenate(heap, values));
    };

    CHECK(join({s("a"), s("b")}) == "\"ab\"");
    CHECK(join({Value::integer(1), Value::integer(2)}) == "\"12\"");
    CHECK(join({Value::number(1.0), s("")}) == "\"1.0\"");
    CHECK(join({s(""), Value::integer(12)}) == "\"12\"");
    CHECK(join({Value::number(-0.0), s("")}) == "\"-0.0\"");
    CHECK(join({Value::number(1e100), s("|")}) == "\"1e+100|\"");
    CHECK(join({s("x"), Value::integer(-3), s("y"), Value::number(0.5)}) == "\"x-3y0.5\"");
    CHECK(join({s(""), s("")}) == "\"\"");
    // strings.lua: ("7" .. 3) and (1 .. "9").
    CHECK(join({s("7"), Value::integer(3)}) == "\"73\"");
    // A single value is left as it is.
    CHECK(join({Value::integer(5)}) == "5");
}

TEST_CASE("V9.1 the result is a normal string: short ones are interned", "[value][coerce]") {
    Heap heap{test_seed};
    const Value a = Value::string(heap.new_string("ab"));
    const Value b = Value::string(heap.new_string("c"));
    const auto joined = lua2::concatenate(heap, std::vector{a, b});
    REQUIRE(joined.has_value());
    CHECK(joined->tag() == lua2::Tag::ShortString);
    CHECK(joined->as_string() == heap.new_string("abc"));

    // Past 40 bytes, a long string, written in one go.
    const Value twenty = Value::string(heap.new_string(std::string(20, 'x')));
    const auto long_join = lua2::concatenate(heap, std::vector{twenty, twenty, b});
    REQUIRE(long_join.has_value());
    CHECK(long_join->tag() == lua2::Tag::LongString);
    CHECK(long_join->as_string()->view() == std::string(40, 'x') + "c");
    // Exactly 40 bytes is still short.
    const auto forty = lua2::concatenate(heap, std::vector{twenty, twenty});
    REQUIRE(forty.has_value());
    CHECK(forty->tag() == lua2::Tag::ShortString);
}

TEST_CASE("V9.1 long chains of ..", "[value][coerce]") {
    Heap heap{test_seed};
    std::vector<Value> values;
    std::string expected;
    for (int n = 0; n < 50; ++n) {
        values.push_back(Value::integer(n));
        expected += std::to_string(n);
    }
    const auto joined = lua2::concatenate(heap, values);
    REQUIRE(joined.has_value());
    CHECK(joined->as_string()->view() == expected);
}

TEST_CASE("V9.2 .. on other values names the one Lua names", "[value][coerce]") {
    Heap heap{test_seed};
    const Value s = Value::string(heap.new_string("s"));
    const Value nil = Value::nil();
    const Value yes = Value::boolean(true);
    const auto join = [&](std::vector<Value> values) {
        return text(lua2::concatenate(heap, values));
    };
    const std::string about_nil = "error: attempt to concatenate a nil value";
    const std::string about_boolean = "error: attempt to concatenate a boolean value";

    CHECK(join({nil, s}) == about_nil);
    CHECK(join({s, nil}) == about_nil);
    CHECK(join({Value::integer(1), yes}) == about_boolean);
    // Both wrong: the left one.
    CHECK(join({yes, nil}) == about_boolean);
    CHECK(join({nil, yes}) == about_nil);
    // Longer chains are joined from the right: the rightmost wrong operand
    // is named, unless it is last and its left neighbor is wrong too.
    CHECK(join({nil, s, yes}) == about_boolean);
    CHECK(join({yes, s, s, nil}) == about_nil);
    CHECK(join({s, nil, yes}) == about_nil);
    CHECK(join({yes, nil, s}) == about_nil);
}
