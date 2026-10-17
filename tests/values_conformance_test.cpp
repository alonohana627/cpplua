#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "gc/heap.hpp"
#include "value/arith.hpp"
#include "value/bitwise.hpp"
#include "value/compare.hpp"
#include "value/convert.hpp"
#include "value/string.hpp"
#include "value/value.hpp"
#include "value_printer.hpp"

// Assertions from the official Lua 5.4.9 test suite (math.lua and
// strings.lua) that only need values, rewritten in C++. Each section names
// where it comes from. The rest of those files needs the VM and the standard
// library, and runs in the conformance stage.

using lua2::Value;
using namespace std::string_view_literals;

namespace {

constexpr std::int64_t minint = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t maxint = std::numeric_limits<std::int64_t>::max();
constexpr int intbits = 64;
constexpr int floatbits = 53;

Value iv(std::int64_t v) {
    return Value::integer(v);
}

Value fv(double v) {
    return Value::number(v);
}

// The result of an operation, failing the test on an error.
Value ok(const lua2::ArithResult& result) {
    if (!result) {
        FAIL("unexpected error: " << result.error().message);
    }
    return *result;
}

// Lua's == on two results.
bool eq(Value a, Value b) {
    return lua2::equals(a, b);
}

// math.lua's same_number: equal, and the same subtype.
bool same_number(Value a, Value b) {
    return lua2::equals(a, b) && a.is_integer() == b.is_integer() && a.is_float() == b.is_float();
}

bool lt(Value a, Value b) {
    const auto result = lua2::less_than(a, b);
    if (!result) {
        FAIL("unexpected error: " << result.error().message);
    }
    return *result;
}

bool le(Value a, Value b) {
    const auto result = lua2::less_equal(a, b);
    if (!result) {
        FAIL("unexpected error: " << result.error().message);
    }
    return *result;
}

bool is_nan(Value v) {
    return v.is_float() && std::isnan(v.as_float());
}

// 2^n as a float, as Lua's 2^n.
Value two_to(int n) {
    return ok(lua2::power(iv(2), iv(n)));
}

} // namespace

TEST_CASE("math.lua: integers and floats in this build", "[value][conformance]") {
    // (1 << intbits) == 0, minint == 1 << (intbits - 1), maxint == minint - 1
    CHECK(eq(ok(lua2::shift_left(iv(1), iv(intbits))), iv(0)));
    CHECK(eq(iv(minint), ok(lua2::shift_left(iv(1), iv(intbits - 1)))));
    CHECK(eq(iv(maxint), ok(lua2::subtract(iv(minint), iv(1)))));

    // The float mantissa has 53 bits: 2^53 + 1.0 rounds back to 2^53.
    const Value x = two_to(floatbits);
    CHECK(lt(ok(lua2::subtract(x, fv(1.0))), x));
    CHECK(eq(x, ok(lua2::add(x, fv(1.0)))));

    // isNaN(0/0) and not isNaN(1/0)
    CHECK(is_nan(ok(lua2::divide(iv(0), iv(0)))));
    CHECK_FALSE(is_nan(ok(lua2::divide(iv(1), iv(0)))));
}

TEST_CASE("math.lua: basic float notation and numeric strings", "[value][conformance]") {
    lua2::Heap heap{1};
    const auto str = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    // 0e12 == 0 and .0 == 0 and 0. == 0 and .2e2 == 20 and 2.E-1 == 0.2
    constexpr auto notations = std::to_array<std::pair<std::string_view, double>>({
        {"0e12", 0.0},
        {".0", 0.0},
        {"0.", 0.0},
        {".2e2", 20.0},
        {"2.E-1", 0.2},
    });
    for (const auto [text, value] : notations) {
        CAPTURE(text);
        const auto number = lua2::string_to_number(text);
        REQUIRE(number.has_value());
        CHECK(eq(number.value_or(Value{}), fv(value)));
    }

    // local a,b,c = "2", " 3e0 ", " 10  "
    const Value a = str("2");
    const Value b = str(" 3e0 ");
    const Value c = str(" 10  ");
    CHECK(eq(ok(lua2::add(a, b)), iv(5)));
    CHECK(eq(ok(lua2::negate(b)), iv(-3)));
    CHECK(eq(ok(lua2::add(b, str("2"))), iv(5)));
    CHECK(eq(ok(lua2::subtract(str("10"), c)), iv(0)));
    CHECK(eq(ok(lua2::negate(c)), ok(lua2::negate(str("  10 ")))));
    CHECK(eq(ok(lua2::modulo(c, a)), iv(0)));
    CHECK(eq(ok(lua2::power(a, b)), iv(8)));
    // The strings themselves are untouched.
    CHECK(lua2::equals(a, str("2")));
    CHECK(lua2::equals(b, str(" 3e0 ")));
    // a = 0; a == -a and 0 == -0
    CHECK(eq(iv(0), ok(lua2::negate(iv(0)))));

    // "2" + 1 == 3, "2 " + 1 == 3, " -2 " + 1 == -1, " -0xa " + 1 == -9
    CHECK(eq(ok(lua2::add(str("2"), iv(1))), iv(3)));
    CHECK(eq(ok(lua2::add(str("2 "), iv(1))), iv(3)));
    CHECK(eq(ok(lua2::add(str(" -2 "), iv(1))), iv(-1)));
    CHECK(eq(ok(lua2::add(str(" -0xa "), iv(1))), iv(-9)));

    // implicit conversions: '10' * '20' == 200 and so on
    const Value ten = str("10");
    const Value twenty = str("20");
    CHECK(eq(ok(lua2::multiply(ten, twenty)), iv(200)));
    CHECK(eq(ok(lua2::add(ten, twenty)), iv(30)));
    CHECK(eq(ok(lua2::subtract(ten, twenty)), iv(-10)));
    CHECK(eq(ok(lua2::divide(ten, twenty)), fv(0.5)));
    CHECK(eq(ok(lua2::negate(twenty)), iv(-20)));
}

TEST_CASE("math.lua: integer arithmetic", "[value][conformance]") {
    CHECK(lt(iv(minint), ok(lua2::add(iv(minint), iv(1)))));
    CHECK(lt(ok(lua2::subtract(iv(maxint), iv(1))), iv(maxint)));
    CHECK(eq(ok(lua2::subtract(iv(0), iv(minint))), iv(minint)));
    CHECK(eq(ok(lua2::multiply(iv(minint), iv(minint))), iv(0)));
    CHECK(
        eq(ok(lua2::multiply(ok(lua2::multiply(iv(maxint), iv(maxint))), iv(maxint))), iv(maxint)));
}

TEST_CASE("math.lua: floor division", "[value][conformance]") {
    // i // j == math.floor(i / j), with i and j as integers and as floats.
    for (const std::int64_t i : {-16, -15, -3, -2, -1, 0, 1, 2, 3, 15}) {
        for (const std::int64_t j : {-16, -15, -3, -2, -1, 1, 2, 3, 15}) {
            const double expected = std::floor(static_cast<double>(i) / static_cast<double>(j));
            for (const bool float_i : {false, true}) {
                for (const bool float_j : {false, true}) {
                    const Value x = float_i ? fv(static_cast<double>(i)) : iv(i);
                    const Value y = float_j ? fv(static_cast<double>(j)) : iv(j);
                    CAPTURE(i, j, float_i, float_j);
                    CHECK(eq(ok(lua2::floor_divide(x, y)), fv(expected)));
                }
            }
        }
    }

    const Value inf = fv(std::numeric_limits<double>::infinity());
    CHECK(eq(ok(lua2::floor_divide(iv(1), fv(0.0))), inf));
    CHECK(eq(ok(lua2::floor_divide(iv(-1), fv(0.0))), ok(lua2::negate(inf))));
    CHECK(same_number(ok(lua2::floor_divide(fv(3.5), fv(1.5))), fv(2.0)));
    CHECK(same_number(ok(lua2::floor_divide(fv(3.5), fv(-1.5))), fv(-3.0)));
    CHECK(eq(ok(lua2::floor_divide(fv(1.0), iv(0))), inf));
    CHECK(same_number(ok(lua2::floor_divide(fv(3.5), iv(1))), fv(3.0)));
    CHECK(same_number(ok(lua2::floor_divide(fv(3.5), iv(-1))), fv(-4.0)));

    CHECK(eq(ok(lua2::floor_divide(iv(maxint), iv(maxint))), iv(1)));
    CHECK(eq(ok(lua2::floor_divide(iv(maxint), iv(1))), iv(maxint)));
    CHECK(eq(ok(lua2::floor_divide(iv(maxint - 1), iv(maxint))), iv(0)));
    CHECK(eq(ok(lua2::floor_divide(iv(maxint), iv(maxint - 1))), iv(1)));
    CHECK(eq(ok(lua2::floor_divide(iv(minint), iv(minint))), iv(1)));
    CHECK(eq(ok(lua2::floor_divide(iv(minint + 1), iv(minint))), iv(0)));
    CHECK(eq(ok(lua2::floor_divide(iv(minint), iv(minint + 1))), iv(1)));
    CHECK(eq(ok(lua2::floor_divide(iv(minint), iv(1))), iv(minint)));
    CHECK(eq(ok(lua2::floor_divide(iv(minint), iv(-1))), ok(lua2::negate(iv(minint)))));
    CHECK(eq(ok(lua2::floor_divide(iv(minint), iv(-2))), two_to(intbits - 2)));
    CHECK(eq(ok(lua2::floor_divide(iv(maxint), iv(-1))), iv(-maxint)));
}

TEST_CASE("math.lua: the mod operator", "[value][conformance]") {
    CHECK(same_number(ok(lua2::modulo(iv(-4), iv(3))), iv(2)));
    CHECK(same_number(ok(lua2::modulo(iv(4), iv(-3))), iv(-2)));
    CHECK(same_number(ok(lua2::modulo(fv(-4.0), iv(3))), fv(2.0)));
    CHECK(same_number(ok(lua2::modulo(iv(4), fv(-3.0))), fv(-2.0)));
    CHECK(same_number(ok(lua2::modulo(iv(4), iv(-5))), iv(-1)));
    CHECK(same_number(ok(lua2::modulo(iv(4), fv(-5.0))), fv(-1.0)));
    CHECK(same_number(ok(lua2::modulo(iv(4), iv(5))), iv(4)));
    CHECK(same_number(ok(lua2::modulo(iv(4), fv(5.0))), fv(4.0)));
    CHECK(same_number(ok(lua2::modulo(iv(-4), iv(-5))), iv(-4)));
    CHECK(same_number(ok(lua2::modulo(iv(-4), fv(-5.0))), fv(-4.0)));
    CHECK(same_number(ok(lua2::modulo(iv(-4), iv(5))), iv(1)));
    CHECK(same_number(ok(lua2::modulo(iv(-4), fv(5.0))), fv(1.0)));
    CHECK(same_number(ok(lua2::modulo(fv(4.25), iv(4))), fv(0.25)));
    CHECK(same_number(ok(lua2::modulo(fv(10.0), iv(2))), fv(0.0)));
    CHECK(same_number(ok(lua2::modulo(fv(-10.0), iv(2))), fv(0.0)));
    CHECK(same_number(ok(lua2::modulo(fv(-10.0), iv(-2))), fv(0.0)));
    // math.pi - math.pi % 1 == 3, math.pi - math.pi % 0.001 == 3.141
    const Value pi = fv(std::numbers::pi);
    CHECK(eq(ok(lua2::subtract(pi, ok(lua2::modulo(pi, iv(1))))), iv(3)));
    CHECK(eq(ok(lua2::subtract(pi, ok(lua2::modulo(pi, fv(0.001))))), fv(3.141)));

    // Integer and float modulo agree.
    for (std::int64_t i = -10; i <= 10; ++i) {
        for (std::int64_t j = -10; j <= 10; ++j) {
            if (j != 0) {
                CAPTURE(i, j);
                CHECK(eq(ok(lua2::modulo(fv(static_cast<double>(i)), iv(j))),
                         ok(lua2::modulo(iv(i), iv(j)))));
            }
        }
    }
    for (int i = 0; i <= 10; ++i) {
        for (std::int64_t j = -10; j <= 10; ++j) {
            if (j != 0) {
                CAPTURE(i, j);
                CHECK(eq(ok(lua2::modulo(two_to(i), iv(j))),
                         ok(lua2::modulo(ok(lua2::shift_left(iv(1), iv(i))), iv(j)))));
            }
        }
    }

    // Precision of modulo for large numbers: (1 << i) % 3 == i % 2 + 1, and
    // the same with 2^i up to the largest float.
    for (int i = 10; i < 63; ++i) {
        CAPTURE(i);
        CHECK(eq(ok(lua2::modulo(ok(lua2::shift_left(iv(1), iv(i))), iv(3))), iv((i % 2) + 1)));
    }
    for (int i = 10; i < 1024; ++i) {
        CAPTURE(i);
        CHECK(eq(ok(lua2::modulo(two_to(i), iv(3))), iv((i % 2) + 1)));
    }

    CHECK(same_number(ok(lua2::modulo(iv(minint), iv(minint))), iv(0)));
    CHECK(same_number(ok(lua2::modulo(iv(maxint), iv(maxint))), iv(0)));
    CHECK(eq(ok(lua2::modulo(iv(minint + 1), iv(minint))), iv(minint + 1)));
    CHECK(eq(ok(lua2::modulo(iv(maxint - 1), iv(maxint))), iv(maxint - 1)));
    CHECK(eq(ok(lua2::modulo(iv(minint), iv(maxint))), iv(maxint - 1)));
    CHECK(eq(ok(lua2::modulo(iv(minint), iv(-1))), iv(0)));
    CHECK(eq(ok(lua2::modulo(iv(minint), iv(-2))), iv(0)));
    CHECK(eq(ok(lua2::modulo(iv(maxint), iv(-2))), iv(-1)));

    // The non-portable section (the Windows C library got fmod wrong once).
    const double huge = std::numeric_limits<double>::infinity();
    CHECK(is_nan(ok(lua2::modulo(fv(0.0), iv(0)))));
    CHECK(is_nan(ok(lua2::modulo(fv(1.3), iv(0)))));
    CHECK(is_nan(ok(lua2::modulo(fv(huge), iv(1)))));
    CHECK(is_nan(ok(lua2::modulo(fv(huge), fv(1e30)))));
    CHECK(is_nan(ok(lua2::modulo(fv(-huge), fv(1e30)))));
    CHECK(is_nan(ok(lua2::modulo(fv(-huge), fv(-1e30)))));
    CHECK(eq(ok(lua2::modulo(iv(1), fv(huge))), iv(1)));
    CHECK(eq(ok(lua2::modulo(fv(1e30), fv(huge))), fv(1e30)));
    CHECK(eq(ok(lua2::modulo(fv(1e30), fv(-huge))), fv(-huge)));
    CHECK(eq(ok(lua2::modulo(iv(-1), fv(huge))), fv(huge)));
    CHECK(eq(ok(lua2::modulo(iv(-1), fv(-huge))), iv(-1)));
}

TEST_CASE("math.lua: order between floats and integers", "[value][conformance]") {
    // The floatbits < intbits branch of math.lua.
    const Value fmax = two_to(floatbits);
    const Value ifmax = ok(lua2::bit_or(fmax, iv(0)));
    CHECK(lt(fmax, ok(lua2::add(ifmax, iv(1)))));
    CHECK(lt(ok(lua2::subtract(fmax, iv(1))), ifmax));
    CHECK_FALSE(le(fmax, ok(lua2::subtract(ifmax, iv(1)))));
    CHECK(lt(iv(maxint), two_to(intbits)));
    CHECK(lt(ok(lua2::negate(two_to(intbits))), iv(minint)));
    CHECK(le(iv(maxint), two_to(intbits)));
    CHECK(le(ok(lua2::negate(two_to(intbits))), iv(minint)));

    // Equality across the border.
    CHECK(eq(two_to(floatbits), ok(lua2::shift_left(iv(1), iv(floatbits)))));
    CHECK_FALSE(eq(ok(lua2::add(two_to(floatbits), fv(1.0))),
                   ok(lua2::add(ok(lua2::shift_left(iv(1), iv(floatbits))), iv(1)))));
    CHECK(eq(ok(lua2::add(iv(minint), fv(0.0))), iv(minint)));
    CHECK(eq(ok(lua2::add(iv(minint), fv(0.0))), ok(lua2::negate(two_to(intbits - 1)))));

    // NaN is not ordered against anything.
    const Value nan = ok(lua2::divide(iv(0), iv(0)));
    CHECK_FALSE(lt(nan, iv(0)));
    CHECK_FALSE(lt(iv(minint), nan)); // not (NaN > minint)
    CHECK_FALSE(le(nan, iv(-9)));
    CHECK_FALSE(le(nan, iv(maxint)));
    CHECK_FALSE(lt(nan, iv(maxint)));
    CHECK_FALSE(le(iv(minint), nan));
    CHECK_FALSE(lt(iv(minint), nan));
    CHECK_FALSE(le(iv(4), nan));
    CHECK_FALSE(lt(iv(4), nan));
}

TEST_CASE("math.lua: floats to integers", "[value][conformance]") {
    // f2i(x) is x | x.
    const auto f2i = [](Value x) { return lua2::bit_or(x, x); };
    const std::string no_integer = "number has no integer representation";
    const double huge = std::numeric_limits<double>::infinity();
    CHECK(f2i(fv(huge)).error().message == no_integer);
    CHECK(f2i(fv(-huge)).error().message == no_integer);
    CHECK(f2i(ok(lua2::divide(iv(0), iv(0)))).error().message == no_integer);
    // checkcompt: 2.3 >> 0, 2.0^63 & 1, 1 | 2.0^63, 2.3 ~ 0.0
    CHECK(lua2::shift_right(fv(2.3), iv(0)).error().message == no_integer);
    CHECK(lua2::bit_and(two_to(intbits - 1), iv(1)).error().message == no_integer);
    CHECK(lua2::bit_or(iv(1), two_to(intbits - 1)).error().message == no_integer);
    CHECK(lua2::bit_xor(fv(2.3), fv(0.0)).error().message == no_integer);

    // The floatbits < intbits branch.
    CHECK(eq(ok(lua2::add(iv(maxint), fv(1.0))), ok(lua2::add(iv(maxint), fv(0.0)))));
    CHECK(eq(ok(lua2::subtract(iv(minint), fv(1.0))), ok(lua2::add(iv(minint), fv(0.0)))));
    CHECK(f2i(ok(lua2::add(iv(maxint), fv(0.0)))).error().message == no_integer);
    CHECK(eq(ok(f2i(two_to(intbits - 2))), ok(lua2::shift_left(iv(1), iv(intbits - 2)))));
    CHECK(eq(ok(f2i(ok(lua2::negate(two_to(intbits - 2))))),
             ok(lua2::negate(ok(lua2::shift_left(iv(1), iv(intbits - 2)))))));
    CHECK(eq(ok(lua2::floor_divide(ok(lua2::add(two_to(floatbits - 1), fv(1.0))), iv(1))),
             ok(lua2::add(ok(lua2::shift_left(iv(1), iv(floatbits - 1))), iv(1)))));
    // The largest integer a float holds, and the next one.
    // maxint - (1 << (intbits - floatbits)) + 1, that is 2^63 - 2^11.
    constexpr std::int64_t mf = maxint - 2048 + 1;
    CHECK(eq(ok(f2i(ok(lua2::add(iv(mf), fv(0.0))))), iv(mf)));
    // mf + 1 has no float: it rounds to another integer (f2i(mf + 0.0) ~= mf).
    CHECK_FALSE(eq(ok(f2i(ok(lua2::add(iv(mf + 1), fv(0.0))))), iv(mf + 1)));
    // minint converts at any precision.
    CHECK(eq(ok(f2i(ok(lua2::add(iv(minint), fv(0.0))))), iv(minint)));
}

TEST_CASE("math.lua: -0 and NaN", "[value][conformance]") {
    const Value mz = fv(-0.0);
    const Value z = fv(0.0);
    CHECK(eq(mz, z));
    CHECK(lt(ok(lua2::divide(iv(1), mz)), iv(0)));
    CHECK(lt(iv(0), ok(lua2::divide(iv(1), z))));
    const Value inf = ok(lua2::add(ok(lua2::multiply(fv(1e308), iv(10))), iv(1)));
    CHECK(eq(ok(lua2::divide(iv(-1), inf)), ok(lua2::divide(iv(1), inf))));

    const Value nan = ok(lua2::subtract(inf, inf));
    CHECK_FALSE(eq(nan, nan));
    CHECK_FALSE(lt(nan, nan));
    CHECK_FALSE(le(nan, nan));
    CHECK_FALSE(lt(iv(0), nan));
    CHECK_FALSE(lt(nan, iv(0)));
    const Value nan1 = ok(lua2::divide(iv(0), iv(0)));
    CHECK_FALSE(eq(nan, nan1));
    CHECK_FALSE(le(nan, nan1));
    CHECK_FALSE(le(nan1, nan));

    // A string with the bytes of 0.0 is not 0.
    lua2::Heap heap{1};
    const Value zeros = Value::string(heap.new_string("\0\0\0\0\0\0\0\0"sv));
    CHECK_FALSE(eq(iv(0), zeros));
}

TEST_CASE("math.lua: tonumber and tostring", "[value][conformance]") {
    const auto number = [](std::string_view s) {
        return lua2::string_to_number(s).value_or(Value{});
    };
    const auto text = [](Value v) {
        return std::string{lua2::number_text(v).value_or(lua2::NumberText{}).view()};
    };
    // tonumber(tostring(maxint)) and minint, the same subtype.
    CHECK(same_number(number(text(iv(maxint))), iv(maxint)));
    CHECK(same_number(number(text(iv(minint))), iv(minint)));
    // Overflow by one in the last digit: a float.
    CHECK(same_number(number("9223372036854775808"), ok(lua2::add(iv(maxint), fv(1.0)))));
    CHECK(same_number(number("-9223372036854775809"), ok(lua2::subtract(iv(minint), fv(1.0)))));
    // Large numbers, and hex wrapping around.
    CHECK(same_number(number("1" + std::string(30, '0')), fv(1e30)));
    CHECK(same_number(number("-1" + std::string(30, '0')), fv(-1e30)));
    CHECK(same_number(number("0x1" + std::string(30, '0')), iv(0)));
    CHECK(same_number(number("0x" + std::string(intbits / 4, 'f')), iv(-1)));
    CHECK(same_number(number("-0x" + std::string(intbits / 4, 'f')), iv(1)));
    CHECK(same_number(number("10000000000000000000000.0"), number("10000000000000000000000")));
}

TEST_CASE("strings.lua: string comparisons", "[value][conformance]") {
    lua2::Heap heap{1};
    const auto str = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    CHECK(lt(str("alo"), str("alo1")));
    CHECK(lt(str(""), str("a")));
    CHECK(lt(str("alo\0alo"sv), str("alo\0b"sv)));
    CHECK(lt(str("alo\0alo\0"sv), str("alo\0alo\0\0"sv)));
    CHECK(lt(str("alo"), str("alo\0"sv)));
    CHECK(lt(str("alo"), str("alo\0"sv)));
    CHECK(lt(str("\0"sv), str("\1")));
    CHECK(lt(str("\0\0"sv), str("\0\1"sv)));
    CHECK(le(str("\1\0a\0a"sv), str("\1\0a\0a"sv)));
    CHECK_FALSE(le(str("\1\0a\0b"sv), str("\1\0a\0a"sv)));
    CHECK(lt(str("\0\0\0"sv), str("\0\0\0\0"sv)));
    CHECK_FALSE(lt(str("\0\0\0\0"sv), str("\0\0\0"sv)));
    CHECK(le(str("\0\0\0"sv), str("\0\0\0\0"sv)));
    CHECK_FALSE(le(str("\0\0\0\0"sv), str("\0\0\0"sv)));
    CHECK(le(str("\0\0\0"sv), str("\0\0\0"sv)));
    CHECK_FALSE(lt(str("\0\0b"sv), str("\0\0a\0"sv)));
}

TEST_CASE("strings.lua: numbers in concatenation", "[value][conformance]") {
    lua2::Heap heap{1};
    const auto str = [&](std::string_view bytes) { return Value::string(heap.new_string(bytes)); };
    const auto join = [&](Value a, Value b) {
        const std::array values{a, b};
        const auto joined = lua2::concatenate(heap, values);
        return joined && joined->is_string() ? std::string{joined->as_string()->view()}
                                             : std::string{"(error)"};
    };
    // '' .. 12 == '12' and 12.0 .. '' == '12.0'
    CHECK(join(str(""), iv(12)) == "12");
    CHECK(join(fv(12.0), str("")) == "12.0");
    // ("7" .. 3) << 1 == 146 is bitwise on a string, which needs
    // bwcoercion.lua; the concatenation part:
    CHECK(join(str("7"), iv(3)) == "73");
    CHECK(join(iv(1), str("9")) == "19");
}
