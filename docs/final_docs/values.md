# Values

How lua2's values work. This page describes the code as it is, and is filled in as stage 4 is
built. The rules are in [../spec/values.md](../spec/values.md), the background and measurements
in [../learn/values.md](../learn/values.md), and the remaining work in
[../plan/values.md](../plan/values.md).

**Status:** parts 0 to 3 of the plan. A `Value` holds `nil`, a boolean, an integer or a float,
and knows its Lua type and its truthiness; numbers convert between integer and float; the
arithmetic and bitwise operators work with Lua's rules and error messages. Still to come:
comparisons (part 4), strings (part 5) and conversions between strings and numbers (parts 6
to 8).

## Where it lives

| File                             | Contents                                             |
| -------------------------------- | ---------------------------------------------------- |
| `lib/internal/value/value.hpp`   | `Type`, `type_name()`, `Tag`, `Value`, `debug_text()` |
| `lib/value/value.cpp`            | its implementation                                   |
| `lib/internal/value/number.hpp`  | conversions between integers and floats (header only, `constexpr`) |
| `lib/internal/value/arith.hpp`   | the arithmetic operators, and their integer and float versions |
| `lib/value/arith.cpp`            | its implementation                                   |
| `lib/internal/value/bitwise.hpp` | the bitwise operators, and their integer versions    |
| `lib/value/bitwise.cpp`          | its implementation                                   |
| `lib/internal/runtime_error.hpp` | `RuntimeError`, an error raised while Lua code runs  |
| `tests/arith_test.cpp`           | arithmetic tests, tagged `[value][arith]`            |
| `tests/bitwise_test.cpp`         | bitwise tests, tagged `[value][bitwise]`             |
| `tests/value_printer.hpp`        | shows a `Value` in Catch2 failure messages           |
| `tests/value_test.cpp`           | tests, tagged `[value]`                              |

Values are internal to the library for now.

## Types and tags

`Type` is Lua's eight types (V1.1), and `type_name(type)` spells them as Lua does (V1.2):
`"nil"`, `"boolean"`, `"number"` and so on.

`Tag` is finer, like Lua's own variant tags: it says what a value holds.

| `Tag`     | `Type`  | Payload                   |
| --------- | ------- | ------------------------- |
| `Nil`     | Nil     | unused                    |
| `False`   | Boolean | unused                    |
| `True`    | Boolean | unused                    |
| `Integer` | Number  | the `std::int64_t`'s bits |
| `Float`   | Number  | the `double`'s bits       |

Booleans have a tag each, so they need no payload. Integer and float are two tags of one type,
which is how a number can be an integer *or* a float (V1.3) while `type()` says `number` for
both.

## `Value`

A `Value` is a `Tag` and an 8-byte payload: 16 bytes, trivially copyable (both checked by
`static_assert`).

```cpp
Value v = Value::integer(3);
v.type();          // Type::Number
v.is_integer();    // true
v.as_integer();    // 3
v.as_float();      // stops the program: v holds an integer
```

**Made only through typed constructors**: `Value::nil()` (or `Value{}`),
`Value::boolean(b)`, `Value::integer(i)`, `Value::number(d)`. The tag and payload are private,
so a value can never claim to be a float while holding an integer's bits.

**The payload is raw bits.** It is a `std::uint64_t`, and the constructors and accessors convert
with `std::bit_cast`. There is no union, so there is no "inactive member" that could be read by
mistake, and everything works at compile time too (`constexpr`).

**Every accessor checks the tag, in every build.** `as_boolean()`, `as_integer()` and
`as_float()` first check that the value holds what is asked for. If not, that is a bug in lua2,
never in the Lua program, and `wrong_access()` prints where the accessor was called and stops
the program:

```
lua2 internal error: lib/value/arith.cpp:42: asked a float value for its integer
```

The caller's location comes from a `std::source_location` default argument. `wrong_access()`
writes with plain `fwrite` calls, so it cannot allocate or throw while reporting. The check is
free in practice: code calls an accessor right after testing the tag (`if (v.is_integer())`),
and the compiler removes the repeated test. That was measured in
[../learn/values.md](../learn/values.md) section 3.

**Predicates**: `is_nil()`, `is_boolean()`, `is_number()`, `is_integer()`, `is_float()`, and
`is_falsy()`/`is_truthy()`. Only `nil` and `false` are falsy (V1.4); `0`, `0.0`, `-0.0` and
`nan` are truthy.

**Storing values apart.** `tag()` and `payload()` take a value apart, and
`Value::from_parts(tag, payload)` puts it back together. They are for the containers of stages
5 and 6 (the stack, table arrays), which store tags and payloads in separate arrays, 9 bytes
per value instead of 16.

**`identical(other)`** is true when both the tag and the payload bits match. It is not Lua's
`==` (part 4): `1` and `1.0` are not identical, and a NaN is identical to itself. Tests and
containers use it.

## Debug text

`debug_text(value)` shows a value for tests and debugging: `nil`, `true`, `false`, `-42`,
`1.0`, `0.5`, `-inf`. A float always looks like a float (`1.0`, not `1`), using the same
`format_float()` as the token dump. This is not Lua's number-to-string conversion (part 6),
which uses `%.14g`. Catch2 prints failed checks with it through a `StringMaker` in
`tests/value_test.cpp`.

`tag_name(tag)` names a tag for messages: `"nil"`, `"false"`, `"true"`, `"integer"`,
`"float"`.

## Integer and float conversions

All in `number.hpp`, all `constexpr`, and tested with `STATIC_REQUIRE` where they can be.

**Integer to float** (V2.3): `int_to_float(i)` is a plain `static_cast<double>`, the nearest
double. Exact up to 2^53; beyond that it rounds, so `math.maxinteger` becomes 2^63.

**Float to integer** (V2.4): `float_to_int(f, rounding)` returns the integer with the same
value, or `std::nullopt`. It works like Lua's `luaV_flttointns`:

1. Take the whole part below `f` with `floor_float()`.
2. If that is not `f`, the float is not whole. `Rounding::Exact` (the default) gives up;
   `Rounding::Floor` keeps the whole part; `Rounding::Ceil` adds one.
3. Check the range: `-2^63 <= whole < 2^63`. Both bounds are exact doubles. Anything outside,
   `inf`, `-inf` and `nan` give `std::nullopt`; `nan` fails every comparison.

Exact is what bitwise operations need (part 3: `3.0 | 0` is `3`, `3.5 | 0` is an error).
Floor and ceil are for exact comparisons between integers and floats (part 4) and later for
`for` loops.

**`floor_float(f)`** is our own floor. libc++'s `std::floor` is not `constexpr`, and without
`-march` it can be a call into the C library. Every double from 2^52 up is already whole, so
only smaller ones are rounded: truncate through `std::int64_t` (exact in that range) and
subtract one when truncation went up, which happens for negative non-whole numbers. Out of
range values, `inf` and `nan` come back unchanged; the range test is written so that `nan` is
out of range, because converting `nan` to an integer would be undefined behavior. A test checks
it against `std::floor` on 16,001 values. Zeros come back unchanged, so `-0.0` keeps its sign, as with `std::floor`; float `//` needs that (part 2).

**On values**: `to_float(value)` and `to_integer(value, rounding)` apply these to a `Value`:
an integer or a float converts, anything else gives `std::nullopt`. Strings that look like
numbers join in part 8.
## Arithmetic

`arith.hpp` has one function per operator, each taking `Value`s and returning
`ArithResult`, which is `std::expected<Value, RuntimeError>`:

| Function             | Lua      | Integers give        | Error                                |
| -------------------- | -------- | -------------------- | ------------------------------------ |
| `add(a, b)`          | `a + b`  | an integer, wrapping | non-numbers                          |
| `subtract(a, b)`     | `a - b`  | an integer, wrapping | non-numbers                          |
| `multiply(a, b)`     | `a * b`  | an integer, wrapping | non-numbers                          |
| `divide(a, b)`       | `a / b`  | a float, always      | non-numbers                          |
| `floor_divide(a, b)` | `a // b` | an integer, floored  | non-numbers, `attempt to divide by zero` |
| `modulo(a, b)`       | `a % b`  | an integer, floored  | non-numbers, `attempt to perform 'n%0'` |
| `power(a, b)`        | `a ^ b`  | a float, always      | non-numbers                          |
| `negate(a)`          | `-a`     | an integer, wrapping | non-numbers                          |

Each follows the same shape: two integers take the integer path (V3.2), otherwise both operands
become floats with `to_float()` and the float path runs (V3.4). `/` and `^` have only the float
path (V3.3). If an operand is not a number, the result is
`attempt to perform arithmetic on a X value` (V3.12), naming the first operand unless it is a
number, then the second, like Lua's `luaG_opinterror`. The type error comes before the check
for division by zero: `nil // 0` complains about `nil`. The message is built by
`operand_error(action, a, b)`, which the bitwise operators share; unary operators pass their
operand twice, as Lua does.

`RuntimeError` is only a message for now. Where the error happened is added by the interpreter
(stage 5), and catching it with `pcall` is stage 10.

### The integer versions

`int_add`, `int_subtract`, `int_multiply` and `int_negate` compute on `std::uint64_t` and
convert back: unsigned arithmetic wraps modulo 2^64, which is exactly Lua's wraparound (V3.5),
while signed overflow in C++ is undefined behavior. They are `constexpr`.

`int_floor_divide(a, b)` and `int_modulo(a, b)` (for `b != 0`) are Lua's `luaV_idiv` and
`luaV_mod`:

- C++'s `/` rounds toward zero, Lua's `//` toward minus infinity. They differ when the signs
  differ and the division is not exact, and then the quotient is one too high: subtract one.
- C++'s `%` has the sign of `a`, Lua's the sign of `b`. When a nonzero remainder has the wrong
  sign, add `b`.
- `b == -1` is handled first, as negation and as `0`, because `mininteger / -1` overflows in
  C++ (on x86 it is a crash).

A test checks `a % b == a - (a // b) * b` for every `a` in -20..20 and `b` in -7..7.

### The float versions

- `float_floor_divide(a, b)` is `floor_float(a / b)`. Division by zero follows IEEE:
  `1 // 0.0` is `inf`, `0 / 0` is `nan`. This is why `floor_float()` keeps the sign of zero:
  `-0.0 // 1` is `-0.0` in Lua.
- `float_modulo(a, b)` is Lua's `luai_nummod`: `std::fmod` (sign of `a`), then add `b` when the
  remainder is nonzero and its sign differs from `b`'s. So `-5.5 % 2` is `0.5`, `5 % -inf` is
  `-inf` and `-5 % -inf` is `-5`.
- `float_power(a, b)` is `std::pow`, except `b == 2`, which is `a * a`, as in Lua.

### Checked against Lua's source

Two details first written from memory were wrong and were corrected against the Lua 5.4.6
source (`lvm.c`, `llimits.h`) and the 5.4.9 test suite: integer `//` by zero says
`attempt to divide by zero` (the suite checks this in `math.lua` and `errors.lua`), and the float
modulo adds `b` only when the remainder and `b` have different signs.
## Bitwise operations

`bitwise.hpp` has `bit_and`, `bit_or`, `bit_xor`, `shift_left`, `shift_right` and `bit_not`
(`&`, `|`, `~`, `<<`, `>>`, unary `~`), returning `ArithResult` like the arithmetic operators.

All of them go through one helper: convert both operands with `to_integer()` (exact rounding,
V4.2), then apply the integer version. The result is always an integer: `3.0 | 0` is `3`.
When a conversion fails, the error follows Lua's `luaT_trybinTM`:

- if an operand is not a number: `attempt to perform bitwise operation on a X value`, through
  `operand_error()` (V4.5). This comes first, so `1.5 & nil` names `nil`;
- otherwise a float had no exact integer value: `number has no integer representation`.

**Strings are never converted** (V4.3). Lua 5.4's core does not convert them, and its string
library has metamethods for arithmetic only. The official suite's `bitwise.lua` passes strings
to `&` only because it first loads `bwcoercion.lua`, which adds bitwise metamethods to strings.

**The integer versions** are `constexpr` and work on the bits as `std::uint64_t`, like Lua's
`intop`: `int_and`, `int_or`, `int_xor`, `int_not`, and the shifts:

- `int_shift_left(x, y)` is Lua's `luaV_shiftl`. Shifts are logical, so `-1 >> 1` is
  `math.maxinteger`. A displacement of 64 or more either way gives 0, and a negative one
  shifts the other way. (In C++, shifting by 64 or more is undefined behavior, so the limits
  are checked before shifting.)
- `int_shift_right(x, y)` is `int_shift_left(x, -y)`, with the negation wrapping:
  `-mininteger` is `mininteger`, a right shift by more than 64, so `1 >> mininteger` is 0.

The tests use the cases of the official suite's `bitwise.lua` that need no strings and no
interpreter.