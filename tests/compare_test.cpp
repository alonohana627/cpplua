#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "value/compare.hpp"
#include "value/value.hpp"
#include "value_printer.hpp"

using cpplua::Value;

namespace {

constexpr std::int64_t minint = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t maxint = std::numeric_limits<std::int64_t>::max();
constexpr double inf = std::numeric_limits<double>::infinity();
constexpr double two_to_53 = 9007199254740992.0;
constexpr double two_to_63 = 9223372036854775808.0;
constexpr std::int64_t int_two_to_53 = 9007199254740992;
constexpr std::int64_t int_two_to_62 = 4611686018427387904;

Value i(std::int64_t v) {
    return Value::integer(v);
}

Value f(double v) {
    return Value::number(v);
}

Value nan() {
    return Value::number(std::nan(""));
}

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

std::string error_of(const cpplua::CompareResult& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

} // namespace

// ---- Part 4: equality and order ----
// Many cases are math.lua's, from the official test suite (its branch for
// floats that cannot represent every integer).

TEST_CASE("V7.1 values of different types are never equal", "[value][compare]") {
    CHECK_FALSE(cpplua::equals(Value::nil(), Value::boolean(false)));
    CHECK_FALSE(cpplua::equals(i(0), Value::boolean(false)));
    CHECK_FALSE(cpplua::equals(i(1), Value::boolean(true)));
    CHECK_FALSE(cpplua::equals(Value::nil(), i(0)));
}

TEST_CASE("V7.2 numbers are equal by value, exactly", "[value][compare]") {
    CHECK(cpplua::equals(i(1), f(1.0)));
    CHECK(cpplua::equals(f(1.0), i(1)));
    CHECK(cpplua::equals(i(0), f(-0.0)));
    CHECK(cpplua::equals(f(0.0), f(-0.0)));
    CHECK_FALSE(cpplua::equals(i(1), f(1.5)));
    CHECK_FALSE(cpplua::equals(i(2), i(3)));

    CHECK(cpplua::equals(f(two_to_53), i(int_two_to_53)));
    CHECK(cpplua::equals(f(two_to_53 - 1.0), f(static_cast<double>(int_two_to_53) - 1.0)));
    CHECK_FALSE(cpplua::equals(f(two_to_53 - 1.0), i(int_two_to_53)));
    // 2^53 + 1 has no float: the float rounds, the integer does not.
    CHECK_FALSE(cpplua::equals(f(two_to_53 + 1.0), i(int_two_to_53 + 1)));
    // maxint + 0.0 rounds up to 2^63.
    CHECK_FALSE(cpplua::equals(i(maxint), f(static_cast<double>(maxint))));
    CHECK(cpplua::equals(f(static_cast<double>(maxint)), f(two_to_63)));
    CHECK(cpplua::equals(f(static_cast<double>(minint)), i(minint)));
    CHECK(cpplua::equals(f(-two_to_63), i(minint)));
    CHECK_FALSE(cpplua::equals(f(two_to_63), i(minint)));
    CHECK_FALSE(cpplua::equals(f(inf), i(maxint)));
}

TEST_CASE("V7.3 nan equals nothing, not even itself", "[value][compare]") {
    const Value n = nan();
    CHECK_FALSE(cpplua::equals(n, n));
    CHECK_FALSE(cpplua::equals(n, nan()));
    CHECK_FALSE(cpplua::equals(n, i(0)));
    CHECK_FALSE(cpplua::equals(i(0), n));
}

TEST_CASE("V7.5 nil and booleans compare by value", "[value][compare]") {
    CHECK(cpplua::equals(Value::nil(), Value::nil()));
    CHECK(cpplua::equals(Value::boolean(true), Value::boolean(true)));
    CHECK(cpplua::equals(Value::boolean(false), Value::boolean(false)));
    CHECK_FALSE(cpplua::equals(Value::boolean(true), Value::boolean(false)));
}

TEST_CASE("V8.2 integers and floats are ordered by value", "[value][compare]") {
    CHECK(lt(i(1), f(1.1)));
    CHECK_FALSE(lt(i(1), f(0.9)));
    CHECK(le(i(1), f(1.1)));
    CHECK_FALSE(le(i(1), f(0.9)));
    CHECK(lt(i(-1), f(-0.9)));
    CHECK_FALSE(lt(i(-1), f(-1.1)));
    CHECK(le(i(-1), f(-0.9)));
    CHECK_FALSE(le(i(-1), f(-1.1)));
    CHECK(lt(i(2), i(3)));
    CHECK(le(i(3), i(3)));
    CHECK(lt(f(2.5), f(3.0)));
    CHECK(le(f(-0.0), f(0.0)));
    CHECK_FALSE(lt(f(-0.0), f(0.0)));
}

TEST_CASE("V8.2 the borders of the integer range", "[value][compare]") {
    const Value minf = f(static_cast<double>(minint)); // exactly -2^63
    CHECK(le(i(minint), minf));
    CHECK(le(minf, i(minint)));
    CHECK_FALSE(lt(i(minint), minf));
    CHECK_FALSE(lt(minf, i(minint)));

    // 2^63 is above every integer.
    CHECK(lt(i(maxint), f(two_to_63)));
    CHECK(le(i(maxint), f(two_to_63)));
    CHECK_FALSE(le(f(two_to_63), i(maxint)));
    CHECK(le(i(minint), f(-two_to_63)));
    CHECK(le(f(-two_to_63), i(minint)));

    // Beyond the range, and the infinities.
    CHECK(lt(i(maxint), f(1e300)));
    CHECK(lt(f(-1e300), i(minint)));
    CHECK(lt(i(maxint), f(inf)));
    CHECK(lt(f(-inf), i(minint)));
    CHECK_FALSE(le(f(inf), i(maxint)));
}

TEST_CASE("V8.2 integers that floats cannot represent", "[value][compare]") {
    // math.lua: fmax = 2^53, the first float after which not every integer
    // has a float.
    const double fmax = two_to_53;
    const std::int64_t ifmax = int_two_to_53;
    CHECK(lt(f(fmax), i(ifmax + 1)));
    CHECK(lt(f(fmax - 1), i(ifmax)));
    CHECK(lt(i(-ifmax), f(-(fmax - 1)))); // -(fmax - 1) > -ifmax
    CHECK_FALSE(le(f(fmax), i(ifmax - 1)));
    CHECK(lt(i(-(ifmax + 1)), f(-fmax)));       // -fmax > -(ifmax + 1)
    CHECK_FALSE(le(i(-(ifmax - 1)), f(-fmax))); // not (-fmax >= -(ifmax - 1))
    CHECK(lt(f((fmax / 2) - 0.5), i(ifmax / 2)));
    CHECK(lt(i(-ifmax / 2), f(-((fmax / 2) - 0.5)))); // -(fmax/2 - 0.5) > -ifmax//2

    // Just past 2^53 each float is an even integer; odd integers fall
    // between them.
    CHECK(lt(i(ifmax + 1), f(fmax + 2)));
    CHECK(lt(f(fmax), i(ifmax + 1)));
    CHECK_FALSE(le(i(ifmax + 3), f(fmax + 2)));
    CHECK(le(i(ifmax + 2), f(fmax + 2)));
    CHECK(le(f(fmax + 2), i(ifmax + 2)));
}

TEST_CASE("V8.2 the exact helpers agree with integer comparison", "[value][compare]") {
    // Around 2^53 and 2^62 every float in the range is an integer, so the
    // answers can be checked against plain integer comparison.
    for (const std::int64_t base : {int_two_to_53, int_two_to_62, -int_two_to_62}) {
        for (std::int64_t di = -4; di <= 4; ++di) {
            for (std::int64_t df = -4; df <= 4; ++df) {
                const std::int64_t a = base + di;
                const auto fl = static_cast<double>(base + df);
                const auto as_int = static_cast<std::int64_t>(fl);
                CAPTURE(a, as_int);
                CHECK(cpplua::int_less_float(a, fl) == (a < as_int));
                CHECK(cpplua::int_less_equal_float(a, fl) == (a <= as_int));
                CHECK(cpplua::float_less_int(fl, a) == (as_int < a));
                CHECK(cpplua::float_less_equal_int(fl, a) == (as_int <= a));
            }
        }
    }
}

TEST_CASE("V8.3 every comparison with nan is false", "[value][compare]") {
    const Value n = nan();
    CHECK_FALSE(lt(n, n));
    CHECK_FALSE(le(n, n));
    CHECK_FALSE(lt(n, i(0)));
    CHECK_FALSE(lt(i(0), n));
    CHECK_FALSE(le(n, i(0)));
    CHECK_FALSE(le(i(0), n));
    CHECK_FALSE(lt(n, f(inf)));
    CHECK_FALSE(le(f(-inf), n));
    // Also beyond 2^53, where the integer path is taken.
    CHECK_FALSE(lt(i(maxint), n));
    CHECK_FALSE(le(n, i(minint)));
    CHECK_FALSE(cpplua::int_less_float(maxint, std::nan("")));
    CHECK_FALSE(cpplua::float_less_equal_int(std::nan(""), minint));
}

TEST_CASE("V8.6 other values cannot be ordered", "[value][compare]") {
    CHECK(error_of(cpplua::less_than(Value::nil(), Value::nil())) ==
          "attempt to compare two nil values");
    CHECK(error_of(cpplua::less_than(Value::boolean(true), Value::boolean(false))) ==
          "attempt to compare two boolean values");
    CHECK(error_of(cpplua::less_than(i(1), Value::nil())) == "attempt to compare number with nil");
    CHECK(error_of(cpplua::less_equal(Value::nil(), f(1.0))) ==
          "attempt to compare nil with number");
    // `1 > nil` is `nil < 1`: the operands swap.
    CHECK(error_of(cpplua::less_than(Value::nil(), i(1))) == "attempt to compare nil with number");
}
