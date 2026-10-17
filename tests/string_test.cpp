#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/heap.hpp"
#include "value/bitwise.hpp"
#include "value/compare.hpp"
#include "value/string.hpp"
#include "value/value.hpp"
#include "value_printer.hpp"

using cpplua::Heap;
using cpplua::Value;
using namespace std::string_view_literals;

namespace {

// Tests use a fixed seed, so hashes are the same every run.
constexpr std::uint32_t test_seed = 12345;

bool lt(Value a, Value b) {
    const auto result = cpplua::less_than(a, b);
    REQUIRE(result.has_value());
    return *result;
}

bool le(Value a, Value b) {
    const auto result = cpplua::less_equal(a, b);
    REQUIRE(result.has_value());
    return *result;
}

// The reference version of Lua's luaS_hash, written as directly as in C.
std::uint32_t reference_hash(std::string_view s, std::uint32_t seed) {
    std::uint32_t h = seed ^ static_cast<std::uint32_t>(s.size());
    for (std::size_t l = s.size(); l > 0; l--) {
        h ^= ((h << 5U) + (h >> 2U) + static_cast<std::uint8_t>(s[l - 1]));
    }
    return h;
}

} // namespace

// ---- Part 5: strings ----

TEST_CASE("V10.1 a string holds any bytes", "[value][string]") {
    Heap heap{test_seed};
    const auto* s = heap.new_string("a\0b\xff"sv);
    CHECK(s->length() == 4);
    CHECK(s->view() == "a\0b\xff"sv);
    // A '\0' follows the bytes, for C.
    CHECK(s->data()[4] == '\0');

    const auto* empty = heap.new_string("");
    CHECK(empty->length() == 0);
    CHECK(empty->view().empty());
    CHECK(empty->data()[0] == '\0');
}

TEST_CASE("V10.2 short strings are interned, long ones are not", "[value][string]") {
    Heap heap{test_seed};
    // The heap's own strings ("__index" and the other metamethod names).
    const std::size_t own = heap.string_count();
    const std::size_t own_short = heap.short_string_count();
    const std::string short_bytes(cpplua::max_short_length, 'x');
    const std::string long_bytes(cpplua::max_short_length + 1, 'x');

    const auto* a = heap.new_string(short_bytes);
    const std::string copy(cpplua::max_short_length, 'x'); // other memory, same bytes
    const auto* b = heap.new_string(copy);
    CHECK(a == b);
    CHECK(a->is_short());
    CHECK(heap.string_count() == own + 1);

    const auto* c = heap.new_string(long_bytes);
    const auto* d = heap.new_string(long_bytes);
    CHECK(c != d);
    CHECK_FALSE(c->is_short());
    CHECK(heap.string_count() == own + 3);
    CHECK(heap.short_string_count() == own_short + 1);

    // Different bytes are different strings, even with the same length.
    CHECK(heap.new_string("ab") != heap.new_string("ba"));
    // '\0' counts: "a" and "a\0" differ.
    CHECK(heap.new_string("a") != heap.new_string("a\0"sv));
}

TEST_CASE("V10.2 interning survives the table growing", "[value][string]") {
    Heap heap{test_seed};
    const std::size_t own = heap.string_count(); // the heap's own strings
    // Far more strings than the 128 first buckets, so the table doubles
    // several times.
    std::vector<const cpplua::String*> made;
    made.reserve(5000);
    for (int n = 0; n < 5000; ++n) {
        made.push_back(heap.new_string(std::to_string(n)));
    }
    for (int n = 0; n < 5000; ++n) {
        CHECK(heap.new_string(std::to_string(n)) == made[static_cast<std::size_t>(n)]);
    }
    CHECK(heap.short_string_count() == own + 5000);
    CHECK(heap.string_count() == own + 5000);
}

TEST_CASE("V10.3 Lua's hash function", "[value][string]") {
    STATIC_REQUIRE(cpplua::string_hash("", 0) == 0);
    // seed ^ length, then one step for the single byte 'a' (97).
    STATIC_REQUIRE(cpplua::string_hash("a", 0) == (1U ^ ((1U << 5U) + (1U >> 2U) + 97U)));

    for (const std::string_view s : {""sv, "a"sv, "hello"sv, "a\0b\xff"sv, "name"sv}) {
        CAPTURE(s);
        CHECK(cpplua::string_hash(s, test_seed) == reference_hash(s, test_seed));
        CHECK(cpplua::string_hash(s, 0xdeadbeef) == reference_hash(s, 0xdeadbeef));
    }

    Heap heap{test_seed};
    CHECK(heap.new_string("hello")->hash() == reference_hash("hello", test_seed));
    // A long string's hash is the same function, computed when asked for.
    const std::string long_bytes(100, 'z');
    CHECK(heap.new_string(long_bytes)->hash() == reference_hash(long_bytes, test_seed));
}

TEST_CASE("V10.3 the seed changes the hashes", "[value][string]") {
    Heap one{1};
    Heap two{2};
    CHECK(one.seed() == 1);
    CHECK(one.new_string("x")->hash() != two.new_string("x")->hash());
}

TEST_CASE("V1.1 strings are values of type string", "[value][string]") {
    Heap heap{test_seed};
    const Value s = Value::string(heap.new_string("hi"));
    CHECK(s.type() == cpplua::Type::String);
    CHECK(s.is_string());
    CHECK(s.tag() == cpplua::Tag::ShortString);
    CHECK(s.is_truthy());
    CHECK(s.as_string()->view() == "hi");
    CHECK(Value::string(heap.new_string(std::string(50, 'a'))).tag() == cpplua::Tag::LongString);
    // Even "" is truthy.
    CHECK(Value::string(heap.new_string("")).is_truthy());
    CHECK(cpplua::debug_text(Value::string(heap.new_string("a\n"))) == "\"a\\n\"");
    // A string rebuilt from its parts is the same string.
    CHECK(Value::from_parts(s.tag(), s.payload()).as_string() == s.as_string());
}

TEST_CASE("V7.4 strings are equal when their bytes are", "[value][string]") {
    Heap heap{test_seed};
    const auto str = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    CHECK(cpplua::equals(str("abc"), str("abc")));
    CHECK_FALSE(cpplua::equals(str("abc"), str("abd")));
    CHECK_FALSE(cpplua::equals(str("a"), str("a\0"sv)));

    // Long strings are separate objects, compared by their bytes.
    const std::string long_bytes(60, 'q');
    CHECK(cpplua::equals(str(long_bytes), str(long_bytes)));
    CHECK_FALSE(cpplua::equals(str(long_bytes), str(long_bytes + "r")));
    CHECK_FALSE(cpplua::equals(str(std::string(41, 'a')), str(std::string(40, 'a'))));

    // V7.1: strings never equal numbers.
    CHECK_FALSE(cpplua::equals(str("1"), Value::integer(1)));
}

TEST_CASE("V8.4 strings order byte by byte", "[value][string]") {
    Heap heap{test_seed};
    const auto str = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    CHECK(lt(str("a"), str("b")));
    CHECK(lt(str("Z"), str("a")));
    CHECK(lt(str(""), str("a")));
    CHECK(lt(str("a"), str("a\0"sv)));
    CHECK(lt(str("abc"), str("abd")));
    CHECK(lt(str("ab"), str("abc")));
    CHECK_FALSE(lt(str("abc"), str("abc")));
    CHECK(le(str("abc"), str("abc")));
    CHECK_FALSE(le(str("b"), str("a")));
    // Bytes are unsigned: "\xff" is after "a".
    CHECK(lt(str("a"), str("\xff")));
    CHECK(lt(str("\x7f"), str("\x80")));
    const std::string long_bytes(60, 'm');
    CHECK(lt(str(long_bytes), str(long_bytes + "n")));
    CHECK(lt(str(long_bytes), str("n")));
}

TEST_CASE("V8.5 strings and numbers do not order", "[value][string]") {
    Heap heap{test_seed};
    const Value two = Value::string(heap.new_string("2"));
    const auto result = cpplua::less_than(Value::integer(1), two);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "attempt to compare number with string");
    CHECK(cpplua::less_equal(two, two).has_value());
}

TEST_CASE("V9.3 the length of a string is its byte count", "[value][string]") {
    Heap heap{test_seed};
    const auto len = [&](std::string_view bytes) {
        const auto result = cpplua::length(Value::string(heap.new_string(bytes)));
        REQUIRE(result.has_value());
        return result->as_integer();
    };
    CHECK(len("") == 0);
    CHECK(len("a\0b"sv) == 3);
    CHECK(len("\xc3\xa9") == 2); // "é" in UTF-8
    CHECK(len(std::string(1000, 'x')) == 1000);
}

TEST_CASE("V9.4 other values have no length (yet)", "[value][string]") {
    CHECK(cpplua::length(Value::nil()).error().message == "attempt to get length of a nil value");
    CHECK(cpplua::length(Value::integer(3)).error().message ==
          "attempt to get length of a number value");
}

TEST_CASE("V4.3 bitwise operations do not convert strings", "[value][string]") {
    Heap heap{test_seed};
    const auto result = cpplua::bit_and(Value::string(heap.new_string("3")), Value::integer(1));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "attempt to perform bitwise operation on a string value");
}
