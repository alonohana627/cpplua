# Values

How lua2's values work. This page describes the code as it is, and is filled in as stage 4 is
built. The rules are in [../spec/values.md](../spec/values.md), the background and measurements
in [../learn/values.md](../learn/values.md), and the remaining work in
[../plan/values.md](../plan/values.md).

**Status:** part 0 of the plan. A `Value` holds `nil`, a boolean, an integer or a float, and
knows its Lua type and its truthiness. Still to come: operations on numbers (parts 1 to 4),
strings (part 5) and conversions (parts 6 to 8).

## Where it lives

| File                             | Contents                                             |
| -------------------------------- | ---------------------------------------------------- |
| `lib/internal/value/value.hpp`   | `Type`, `type_name()`, `Tag`, `Value`, `debug_text()` |
| `lib/value/value.cpp`            | its implementation                                   |
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
