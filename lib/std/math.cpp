// The math library: a port of Lua 5.4.9's lmathlib.c
// (docs/final_docs/stdlib.md), with LUA_COMPAT_MATHLIB's functions as the
// reference Lua has them. math.random is Lua's xoshiro256**, so a seed
// gives the same numbers as in the real Lua.

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <numbers>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

void push_number(State& state, double d) {
    push(state, Value::number(d));
}

// A function of one number.
template <double (*F)(double)>
int math_unary(State& state) {
    double x = 0;
    if (!check_number(state, 1, x)) {
        return c_function_error;
    }
    push_number(state, F(x));
    return 1;
}

double c_sin(double x) {
    return std::sin(x);
}

double c_cos(double x) {
    return std::cos(x);
}

double c_tan(double x) {
    return std::tan(x);
}

double c_asin(double x) {
    return std::asin(x);
}

double c_acos(double x) {
    return std::acos(x);
}

double c_exp(double x) {
    return std::exp(x);
}

double c_sqrt(double x) {
    return std::sqrt(x);
}

double c_cosh(double x) {
    return std::cosh(x);
}

double c_sinh(double x) {
    return std::sinh(x);
}

double c_tanh(double x) {
    return std::tanh(x);
}

double c_log10(double x) {
    return std::log10(x);
}

double to_degrees(double x) {
    return x * (180.0 / std::numbers::pi);
}

double to_radians(double x) {
    return x * (std::numbers::pi / 180.0);
}

int math_abs(State& state) {
    const Value v = arg(state, 1);
    if (v.is_integer()) {
        const std::int64_t n = v.as_integer();
        push(state, Value::integer(
                        n < 0 ? static_cast<std::int64_t>(0U - static_cast<std::uint64_t>(n)) : n));
        return 1;
    }
    double x = 0;
    if (!check_number(state, 1, x)) {
        return c_function_error;
    }
    push_number(state, std::fabs(x));
    return 1;
}

int math_atan(State& state) {
    double y = 0;
    double x = 0;
    if (!check_number(state, 1, y) || !opt_number(state, 2, 1, x)) {
        return c_function_error;
    }
    push_number(state, std::atan2(y, x));
    return 1;
}

// lua_tointegerx: a number or numeric string with an exact integer value.
std::optional<std::int64_t> to_integer_value(Value v) {
    if (v.is_string()) {
        const auto n = string_to_number(v.as_string()->view());
        return n ? to_integer(*n) : std::nullopt;
    }
    return to_integer(v);
}

int math_tointeger(State& state) {
    if (const auto n = to_integer_value(arg(state, 1))) {
        push(state, Value::integer(*n));
        return 1;
    }
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    push(state, Value::nil());
    return 1;
}

// pushnumint: an integer if the float has one.
void push_float_or_integer(State& state, double d) {
    if (const auto n = float_to_int(d, Rounding::Exact)) {
        push(state, Value::integer(*n));
    } else {
        push_number(state, d);
    }
}

template <double (*Round)(double)>
int math_round(State& state) {
    if (arg(state, 1).is_integer()) {
        push(state, arg(state, 1)); // an integer is its own floor and ceiling
        return 1;
    }
    double x = 0;
    if (!check_number(state, 1, x)) {
        return c_function_error;
    }
    push_float_or_integer(state, Round(x));
    return 1;
}

double c_floor(double x) {
    return std::floor(x);
}

double c_ceil(double x) {
    return std::ceil(x);
}

int math_fmod(State& state) {
    const Value a = arg(state, 1);
    const Value b = arg(state, 2);
    if (a.is_integer() && b.is_integer()) {
        const std::int64_t d = b.as_integer();
        if (static_cast<std::uint64_t>(d) + 1U <= 1U) { // -1 or 0
            if (d == 0) {
                return arg_error(state, 2, "zero");
            }
            push(state, Value::integer(0)); // no overflow with minint / -1
        } else {
            push(state, Value::integer(a.as_integer() % d));
        }
        return 1;
    }
    double x = 0;
    double y = 0;
    if (!check_number(state, 1, x) || !check_number(state, 2, y)) {
        return c_function_error;
    }
    push_number(state, std::fmod(x, y));
    return 1;
}

int math_modf(State& state) {
    if (arg(state, 1).is_integer()) {
        push(state, arg(state, 1)); // its own integer part
        push_number(state, 0);
        return 2;
    }
    double n = 0;
    if (!check_number(state, 1, n)) {
        return c_function_error;
    }
    const double ip = n < 0 ? std::ceil(n) : std::floor(n); // toward zero
    push_float_or_integer(state, ip);
    push_number(state, n == ip ? 0.0 : n - ip); // the test is for inf and -inf
    return 2;
}

int math_ult(State& state) {
    std::int64_t a = 0;
    std::int64_t b = 0;
    if (!check_integer(state, 1, a) || !check_integer(state, 2, b)) {
        return c_function_error;
    }
    push(state, Value::boolean(static_cast<std::uint64_t>(a) < static_cast<std::uint64_t>(b)));
    return 1;
}

int math_log(State& state) {
    double x = 0;
    if (!check_number(state, 1, x)) {
        return c_function_error;
    }
    double res = 0;
    if (arg_is_none_or_nil(state, 2)) {
        res = std::log(x);
    } else {
        double base = 0;
        if (!check_number(state, 2, base)) {
            return c_function_error;
        }
        if (base == 2.0) {
            res = std::log2(x);
        } else if (base == 10.0) {
            res = std::log10(x);
        } else {
            res = std::log(x) / std::log(base);
        }
    }
    push_number(state, res);
    return 1;
}

// math.min and math.max: the first argument that is least (most) by <.
template <bool Max>
int math_min_max(State& state) {
    const int n = arg_count(state);
    if (n < 1) {
        return arg_error(state, 1, "value expected");
    }
    int best = 1;
    for (int i = 2; i <= n; ++i) {
        bool lt = false;
        const Status s = Max ? compare_values(state, arg(state, best), arg(state, i), false, lt)
                             : compare_values(state, arg(state, i), arg(state, best), false, lt);
        if (s != Status::Ok) {
            return c_function_error;
        }
        if (lt) {
            best = i;
        }
    }
    push(state, arg(state, best));
    return 1;
}

int math_type(State& state) {
    const Value v = arg(state, 1);
    if (v.is_number()) {
        push_string(state, v.is_integer() ? "integer" : "float");
        return 1;
    }
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    push(state, Value::nil());
    return 1;
}

int math_pow(State& state) {
    double x = 0;
    double y = 0;
    if (!check_number(state, 1, x) || !check_number(state, 2, y)) {
        return c_function_error;
    }
    push_number(state, std::pow(x, y));
    return 1;
}

int math_frexp(State& state) {
    double x = 0;
    if (!check_number(state, 1, x)) {
        return c_function_error;
    }
    int e = 0;
    push_number(state, std::frexp(x, &e));
    push(state, Value::integer(e));
    return 2;
}

int math_ldexp(State& state) {
    double x = 0;
    std::int64_t e = 0;
    if (!check_number(state, 1, x) || !check_integer(state, 2, e)) {
        return c_function_error;
    }
    push_number(state, std::ldexp(x, static_cast<int>(e)));
    return 1;
}

// ==== Pseudo-random numbers: xoshiro256** ====

using Rand64 = std::uint64_t;

struct RandomState {
    std::array<Rand64, 4> s;
};

Rand64 rotl(Rand64 x, int n) {
    return (x << static_cast<unsigned>(n)) | (x >> static_cast<unsigned>(64 - n));
}

Rand64 next_random(RandomState& state) {
    auto& s = state.s;
    const Rand64 state0 = s[0];
    const Rand64 state1 = s[1];
    const Rand64 state2 = s[2] ^ state0;
    const Rand64 state3 = s[3] ^ state1;
    const Rand64 res = rotl(state1 * 5, 7) * 9;
    s[0] = state0 ^ state3;
    s[1] = state1 ^ state2;
    s[2] = state2 ^ (state1 << 17U);
    s[3] = rotl(state3, 45);
    return res;
}

// I2d: a float in [0, 1) from the 53 high bits.
double to_unit_float(Rand64 x) {
    constexpr int figs = std::numeric_limits<double>::digits; // FIGS
    constexpr double scale =
        0.5 / static_cast<double>(Rand64{1} << static_cast<unsigned>(figs - 1));
    const auto sx = static_cast<std::int64_t>(x >> static_cast<unsigned>(64 - figs));
    double res = static_cast<double>(sx) * scale;
    if (sx < 0) {
        res += 1.0;
    }
    return res;
}

// project: a random integer in [0, n], without bias.
std::uint64_t project(std::uint64_t ran, std::uint64_t n, RandomState& state) {
    if ((n & (n + 1)) == 0) { // n + 1 is a power of 2
        return ran & n;
    }
    std::uint64_t lim = n; // the smallest 2^b - 1 not smaller than n
    lim |= lim >> 1U;
    lim |= lim >> 2U;
    lim |= lim >> 4U;
    lim |= lim >> 8U;
    lim |= lim >> 16U;
    lim |= lim >> 32U;
    while ((ran &= lim) > n) { // not inside [0, n]: try again
        ran = next_random(state);
    }
    return ran;
}

RandomState& random_state(const State& state) {
    const auto* closure = state.get(state.ci->func).as_c_closure();
    return *static_cast<RandomState*>(closure->upvalues()[0].as_userdata()->data());
}

int math_random(State& state) {
    RandomState& rs = random_state(state);
    const Rand64 rv = next_random(rs);
    std::int64_t low = 0;
    std::int64_t up = 0;
    switch (arg_count(state)) {
    case 0: push_number(state, to_unit_float(rv)); return 1;
    case 1:
        low = 1;
        if (!check_integer(state, 1, up)) {
            return c_function_error;
        }
        if (up == 0) { // a full random integer
            push(state, Value::integer(static_cast<std::int64_t>(rv)));
            return 1;
        }
        break;
    case 2:
        if (!check_integer(state, 1, low) || !check_integer(state, 2, up)) {
            return c_function_error;
        }
        break;
    default: return raise_error(state, "wrong number of arguments");
    }
    if (low > up) {
        return arg_error(state, 1, "interval is empty");
    }
    const std::uint64_t p =
        project(rv, static_cast<std::uint64_t>(up) - static_cast<std::uint64_t>(low), rs);
    push(state, Value::integer(static_cast<std::int64_t>(p + static_cast<std::uint64_t>(low))));
    return 1;
}

void set_seed(RandomState& rs, std::uint64_t n1, std::uint64_t n2) {
    rs.s = {n1, 0xff, n2, 0}; // 0xff avoids a zero state
    for (int i = 0; i < 16; ++i) {
        next_random(rs); // discard the first values to spread the seed
    }
}

// randseed: the time and an address.
std::pair<std::uint64_t, std::uint64_t> fresh_seed(const void* address) {
    return {static_cast<std::uint64_t>(std::time(nullptr)), std::bit_cast<std::uintptr_t>(address)};
}

int math_randomseed(State& state) {
    RandomState& rs = random_state(state);
    std::uint64_t n1 = 0;
    std::uint64_t n2 = 0;
    if (arg_is_none(state, 1)) {
        std::tie(n1, n2) = fresh_seed(&state);
    } else {
        std::int64_t a = 0;
        std::int64_t b = 0;
        if (!check_integer(state, 1, a) || !opt_integer(state, 2, 0, b)) {
            return c_function_error;
        }
        n1 = static_cast<std::uint64_t>(a);
        n2 = static_cast<std::uint64_t>(b);
    }
    set_seed(rs, n1, n2);
    push(state, Value::integer(static_cast<std::int64_t>(n1)));
    push(state, Value::integer(static_cast<std::int64_t>(n2)));
    return 2;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array math_functions{
    LibraryFunction{.name = "abs", .function = &math_abs},
    LibraryFunction{.name = "acos", .function = &math_unary<c_acos>},
    LibraryFunction{.name = "asin", .function = &math_unary<c_asin>},
    LibraryFunction{.name = "atan", .function = &math_atan},
    LibraryFunction{.name = "ceil", .function = &math_round<c_ceil>},
    LibraryFunction{.name = "cos", .function = &math_unary<c_cos>},
    LibraryFunction{.name = "deg", .function = &math_unary<to_degrees>},
    LibraryFunction{.name = "exp", .function = &math_unary<c_exp>},
    LibraryFunction{.name = "tointeger", .function = &math_tointeger},
    LibraryFunction{.name = "floor", .function = &math_round<c_floor>},
    LibraryFunction{.name = "fmod", .function = &math_fmod},
    LibraryFunction{.name = "ult", .function = &math_ult},
    LibraryFunction{.name = "log", .function = &math_log},
    LibraryFunction{.name = "max", .function = &math_min_max<true>},
    LibraryFunction{.name = "min", .function = &math_min_max<false>},
    LibraryFunction{.name = "modf", .function = &math_modf},
    LibraryFunction{.name = "rad", .function = &math_unary<to_radians>},
    LibraryFunction{.name = "sin", .function = &math_unary<c_sin>},
    LibraryFunction{.name = "sqrt", .function = &math_unary<c_sqrt>},
    LibraryFunction{.name = "tan", .function = &math_unary<c_tan>},
    LibraryFunction{.name = "type", .function = &math_type},
    // LUA_COMPAT_MATHLIB
    LibraryFunction{.name = "atan2", .function = &math_atan},
    LibraryFunction{.name = "cosh", .function = &math_unary<c_cosh>},
    LibraryFunction{.name = "sinh", .function = &math_unary<c_sinh>},
    LibraryFunction{.name = "tanh", .function = &math_unary<c_tanh>},
    LibraryFunction{.name = "pow", .function = &math_pow},
    LibraryFunction{.name = "frexp", .function = &math_frexp},
    LibraryFunction{.name = "ldexp", .function = &math_ldexp},
    LibraryFunction{.name = "log10", .function = &math_unary<c_log10>},
};

} // namespace

void open_math(Heap& heap) {
    Table* library = open_library(heap, "math", math_functions);
    const auto set = [&](std::string_view name, Value v) {
        library->set(heap, Value::string(heap.new_string(name)), v);
    };
    set("pi", Value::number(std::numbers::pi));
    set("huge", Value::number(std::numeric_limits<double>::infinity()));
    set("maxinteger", Value::integer(std::numeric_limits<std::int64_t>::max()));
    set("mininteger", Value::integer(std::numeric_limits<std::int64_t>::min()));
    // setrandfunc: random and randomseed share their state, a userdata.
    Userdata* box = heap.new_userdata(sizeof(RandomState), 0);
    auto* rs = new (box->data()) RandomState{}; // NOLINT(cppcoreguidelines-owning-memory): trivial
    const auto [n1, n2] = fresh_seed(&heap);
    set_seed(*rs, n1, n2);
    for (const auto& [name, function] :
         {std::pair{"random", &math_random}, std::pair{"randomseed", &math_randomseed}}) {
        CClosure* closure = heap.new_c_closure(function, 1);
        closure->upvalues()[0] = Value::userdata(box);
        set(name, Value::c_closure(closure));
    }
}

} // namespace cpplua
