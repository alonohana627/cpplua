# Values

How lua2's values work. This page describes the code as it is, and is filled in as stage 4 is
built. The rules are in [../spec/values.md](../spec/values.md), the background and measurements
in [../learn/values.md](../learn/values.md), and the remaining work in
[../plan/values.md](../plan/values.md).

**Status:** parts 0 to 5 of the plan. A `Value` holds `nil`, a boolean, an integer, a float or
a string, and knows its Lua type and its truthiness; numbers convert between integer and
float; the arithmetic, bitwise and comparison operators work with Lua's rules and error
messages; strings are interned like Lua's and have a length; numbers convert to text like
Lua's `tostring`, and strings convert to numbers like Lua's `tonumber`. Still to come: the
coercions that use both directions (part 8) and the conformance sweep (part 9).

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
| `lib/internal/value/compare.hpp` | `==`, `<`, `<=`, and the exact integer/float comparisons |
| `lib/value/compare.cpp`          | its implementation                                   |
| `lib/internal/value/string.hpp`  | `String`, the `Heap` that owns strings, the hash, `length()` |
| `lib/value/string.cpp`           | its implementation                                   |
| `lib/internal/value/convert.hpp` | conversions between numbers and text                 |
| `lib/value/convert.cpp`          | its implementation                                   |
| `lib/internal/numeral.hpp`       | reading numerals, shared with the lexer              |
| `lib/numeral.cpp`                | its implementation                                   |
| `lib/internal/runtime_error.hpp` | `RuntimeError`, an error raised while Lua code runs  |
| `tests/arith_test.cpp`           | arithmetic tests, tagged `[value][arith]`            |
| `tests/bitwise_test.cpp`         | bitwise tests, tagged `[value][bitwise]`             |
| `tests/compare_test.cpp`         | comparison tests, tagged `[value][compare]`          |
| `tests/string_test.cpp`          | string tests, tagged `[value][string]`               |
| `tests/convert_test.cpp`         | conversion tests, tagged `[value][convert]`          |
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
| `ShortString` | String | a `const String*`     |
| `LongString`  | String | a `const String*`     |

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
`Value::boolean(b)`, `Value::integer(i)`, `Value::number(d)`, and `Value::string(s)` for a
string made by a `Heap` (defined in `string.hpp`, since it needs to know whether `s` is short). The tag and payload are private,
so a value can never claim to be a float while holding an integer's bits.

**The payload is raw bits.** It is a `std::uint64_t`, and the constructors and accessors convert
with `std::bit_cast`. There is no union, so there is no "inactive member" that could be read by
mistake, and everything works at compile time too (`constexpr`).

**Every accessor checks the tag, in every build.** `as_boolean()`, `as_integer()`,
`as_float()` and `as_string()` first check that the value holds what is asked for. If not, that is a bug in lua2,
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

**Predicates**: `is_nil()`, `is_boolean()`, `is_number()`, `is_integer()`, `is_float()`,
`is_string()`, and
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
## Equality and order

`compare.hpp` has three functions; the other three operators are built from them:

| Lua      | Call                          |
| -------- | ----------------------------- |
| `a == b` | `equals(a, b)`                |
| `a ~= b` | `!equals(a, b)`               |
| `a < b`  | `less_than(a, b)`             |
| `a <= b` | `less_equal(a, b)`            |
| `a > b`  | `less_than(b, a)`             |
| `a >= b` | `less_equal(b, a)`            |

**`equals(a, b)`** returns a plain `bool`: `==` never fails (V7). Like Lua's `luaV_equalobj`:

- Different tags: only an integer and a float can still be equal, when the float has an exact
  integer value equal to the integer (`to_integer()` with exact rounding). So `1 == 1.0`, but
  `maxint == maxint + 0.0` is false: the float is 2^63.
- Same tag: nil and the booleans are equal (the tag says it all), integers compare as
  integers, floats as floats, so `nan` equals nothing, itself included, and `0.0 == -0.0`.
  Short strings are interned, so two are equal exactly when they are the same `String`: one
  pointer comparison. Long strings compare their bytes. A short and a long string have
  different tags and lengths, so they are never equal.

Metamethods (`__eq`) come with tables, in stage 8.

**`less_than(a, b)` and `less_equal(a, b)`** return `CompareResult`, a
`std::expected<bool, RuntimeError>`. Two numbers compare by value, and two strings byte by byte
as unsigned bytes (V8.4, through `std::string_view::compare`, which compares like `memcmp`); a
string is never ordered against a number (V8.5). Anything else is an error (V8.6), until
metamethods (stage 8):

- `attempt to compare two X values` when both type names are the same,
- `attempt to compare X with Y` otherwise.

The names come in the order compared. For `a > b` that is `b` first, since `a > b` is
`less_than(b, a)`: `1 > nil` says `attempt to compare nil with number`, as Lua does.

### Exact integer/float comparison

Comparing an integer with a float by converting the integer would round integers beyond 2^53,
and `maxint < 2^63` would come out false. The four `constexpr` helpers, `int_less_float`,
`int_less_equal_float`, `float_less_int` and `float_less_equal_int`, are Lua's `LTintfloat`,
`LEintfloat`, `LTfloatint` and `LEfloatint`:

1. If the integer is within ±2^53 (`fits_float()`, Lua's `l_intfitsf`), it converts to float
   exactly, and the two floats are compared. This is the common, fast case.
2. Otherwise the float is rounded to an integer in the direction that keeps the answer:
   `i < f` exactly when `i < ceil(f)`, `i <= f` when `i <= floor(f)`, `f < i` when
   `floor(f) < i`, `f <= i` when `ceil(f) <= i`. Then two integers are compared.
3. If that rounding fails, the float is beyond every integer (or is `nan`): `i < f` is `f > 0`,
   `f < i` is `f < 0`, and `nan` gives false.

The tests use `math.lua`'s border cases (2^53, 2^63, `minint`), check every comparison with
`nan`, and compare the helpers with plain integer comparison on 81 pairs around 2^53 and ±2^62,
where every float is a whole number.
## Strings

`string.hpp` has the `String` class, the `Heap` that makes and owns strings, Lua's hash, and
`length()`.

### `String`

An immutable sequence of bytes, `\0` included (V10.1): `length()`, `data()`, `view()`,
`is_short()` and `hash()`. Only a `Heap` makes strings (the constructor is private), and values
refer to them by `const String*`.

The header and the bytes are **one allocation**, as in Lua: `Heap::allocate()` asks
`::operator new` for `sizeof(String) + length + 1` bytes, constructs the header at the start
with placement `new`, and copies the bytes right after it, followed by a `\0` for C functions.
`data()` finds the bytes at `this + 1`. One allocation per string, and the bytes sit next to
the length in memory.

The header is 32 bytes: two links for the Heap (`next_object_`, `next_in_bucket_`), the
length, the hash, and two flags.

### The hash

`string_hash(bytes, seed)` is Lua's `luaS_hash` (V10.3), `constexpr`: start from
`seed ^ length`, then for each byte from the last to the first,
`h ^= (h << 5) + (h >> 2) + byte`, all in `std::uint32_t`. A test checks it against a direct
transcription of Lua's C code.

A short string's hash is computed when it is made, since interning needs it. A long string's
is computed the first time `hash()` is called, like Lua's `luaS_hashlongstr`: until then the
`hash_` field holds the seed. Most long strings are never table keys, so most never pay for it.

### `Heap`

`heap.new_string(bytes)` returns the string for `bytes`:

- **Short strings** (up to `max_short_length`, 40 bytes) are **interned** (V10.2), like Lua's
  `internshrstr`: hash the bytes, look in the bucket `hash & (bucket_count - 1)`, and walk its
  chain comparing bytes. If found, that string is returned; otherwise a new one is made and put
  at the head of the chain. The table starts with 128 buckets (Lua's `MINSTRTABSIZE`) and
  doubles when it holds as many strings as buckets (`grow_buckets()`, Lua's `luaS_resize`). The
  bucket count is a power of two, so a mask finds the bucket instead of a division.
- **Long strings** are made anew each time, and not hashed yet.

**Ownership.** Every string is on the Heap's `objects_` list, newest first, and `~Heap()` frees
them all. Until the garbage collector (stage 9) takes the Heap's place, a string lives as long
as its Heap. Values only point at strings; they never own them.

**The seed.** `Heap()` picks a random seed with `std::random_device`, so hashes, and later the
order of `pairs`, change from run to run, as in Lua. `Heap(seed)` fixes it; the tests do that.

The Heap is neither copyable nor movable: strings' links point into it.

### Length

`length(value)` is `#` (V9.3): a string's length in bytes, as an integer. Anything else is
`attempt to get length of a X value` (V9.4), until tables (stage 6).

### Memory checked

Besides the tests, the string code was run under AddressSanitizer
(`clang++ -fsanitize=address`) making 20,000 strings, short and long, through several table
growths, with no errors.
## Numbers to strings

`convert.hpp` turns a number into its text, the way Lua's `tostringbuff` does (V6). This is
what `tostring`, `print` and concatenation (part 8) will show.

The text lives in a `NumberText`: a 44-byte buffer (Lua's `MAXNUMBER2STR`) and a length,
returned by value, so making it never allocates; `view()` reads it. A `String` is made from it
only when one is needed.

- **`integer_text(i)`** writes the integer in decimal with `std::to_chars` (V6.1), what Lua's
  `"%lld"` writes: `-9223372036854775808` included.
- **`float_text(f)`** (V6.2):
  1. `inf`, `-inf`, `nan` and `-nan` are written directly (V6.3), from the sign bit for `nan`.
     They are the same on every platform: on Windows, `std::to_chars` would write `-nan(ind)`.
  2. Every other float goes through `std::to_chars` with `chars_format::general` and precision
     14, which the C++ standard defines as exactly what `printf("%.14g")` writes, Lua's
     `LUAI_NUMFFORMAT`, without `printf`'s locale or varargs.
  3. If the result has only digits and `-`, it would read back as an integer, so `.0` is
     added: `1.0`, `-0.0`, `10000000000000.0`. Text with an exponent or a point is left alone:
     `1e+14`, `0.1`, `3.1415926535898`.
- **`number_text(value)`** does either for a number value, and returns nothing for others.

Fourteen digits is fewer than a double needs to read back exactly (17), so `0.1 + 0.2` shows as
`0.3`, as in Lua. The tests use `strings.lua`'s cases and the borders of `%g`: the switch to an
exponent at `1e14` and below `0.0001`, the smallest and largest doubles, 2^53 and 2^63.
## Strings to numbers

`string_to_number(text)` in `convert.hpp` is the number a string holds, or nothing (V5): what
`"10" + 1` (part 8) and `tonumber("10")` (stage 11) will use. It follows Lua's `luaO_str2num`:

1. Trim spaces at both ends: space, `\t`, `\n`, `\v`, `\f`, `\r` (Lua's `lisspace`). Nothing left
   is no number (V5.1).
2. Refuse anything with an `n` or `N` (V5.4): no `inf`, `nan` or `infinity`, which C's `strtod`
   and C++'s `from_chars` would accept. Lua's `l_str2d` does the same.
3. Take one optional `-` or `+` (V5.2). The numeral must start right after it, so `- 1` and
   `--1` are not numbers.
4. Read the rest with `integer_numeral(text, negative)` (V5.3). `negative` lets
   `-9223372036854775808` be an integer, as in Lua's `l_str2int`; any other decimal too big
   for an integer fails here.
5. Otherwise read it with `float_numeral(text)` (V5.4) and apply the sign.

The numeral readers are the lexer's own, moved to `lib/internal/numeral.hpp` so both use them,
as Lua's lexer and `tonumber` both use `luaO_str2num`. The rules therefore match exactly:
hex integers wrap (`"0xffffffffffffffff"` is `-1`), hex floats work (`" 0x2.5 "` is `2.3125`),
and `1e400` is `inf`. A `\0` anywhere is never part of a numeral, so such strings never
convert (V5.5). The result keeps the subtype it read as (V5.6): `"10"` is an integer,
`"10.0"` and `"1e1"` are floats.

The tests use `math.lua`'s `tonumber` cases without a base (about 80 strings, valid and not),
and check that numbers read back from their own text.