# Values

How lua2's values work. This page describes the code as it is, and is filled in as stage 4 is
built. The rules are in [../spec/values.md](../spec/values.md), the background and measurements
in [../learn/values.md](../learn/values.md), and the remaining work in
[../plan/values.md](../plan/values.md).

**Status:** parts 0 and 1 of the plan. A `Value` holds `nil`, a boolean, an integer or a float,
and knows its Lua type and its truthiness; numbers convert between integer and float. Still to
come: operations on numbers (parts 2 to 4), strings (part 5) and conversions between strings
and numbers (parts 6 to 8).

## Where it lives

| File                             | Contents                                             |
| -------------------------------- | ---------------------------------------------------- |
| `lib/internal/value/value.hpp`   | `Type`, `type_name()`, `Tag`, `Value`, `debug_text()` |
| `lib/value/value.cpp`            | its implementation                                   |
| `lib/internal/value/number.hpp`  | conversions between integers and floats (header only, `constexpr`) |
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
it against `std::floor` on 16,001 values. (`floor_float(-0.0)` is `0.0`; the sign of zero does
not matter for any of its uses.)

**On values**: `to_float(value)` and `to_integer(value, rounding)` apply these to a `Value`:
an integer or a float converts, anything else gives `std::nullopt`. Strings that look like
numbers join in part 8.