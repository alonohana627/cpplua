# Values plan

Roadmap stage 4, cut into small parts. Each part adds one group of rules with its own tests, and
ends with everything green (`build`, `ctest`, `tidy`, `format-check`). Rule ids such as `V3.6`
point to [../spec/values.md](../spec/values.md). You only need the rules listed for the current
part.

Background reading is in [../learn/values.md](../learn/values.md). How the finished code works
will be recorded in [../final_docs/values.md](../final_docs/values.md).

Finishing part 9 completes the stage and bumps the version to 0.0.4.

## Decisions to make in part 0

- **How a `Value` is held.** Options, measured in [../learn/values.md](../learn/values.md)
  section 3:
  - A. a tagged union, Lua's own layout, 16 bytes;
  - B. `std::variant<Nil, bool, std::int64_t, double, String*>`, 16 bytes;
  - C. NaN-boxing, 8 bytes, with integers beyond 48 bits boxed on the heap (C2 in the
    measurements);
  - D. tags and payloads in separate arrays, 9 bytes per stored value.

  These answer two questions. **A value in flight** (an operand, a result) is one C++ object:
  A, B or C. **Many stored values** (the stack, table arrays) can use D. In the benchmark D was
  as fast as NaN-boxing, with all 64 bits and no heap boxing, and 4 to 16 times faster on
  integers than the 16-byte forms.

  **Decided** (the rule: the most efficient option that does not risk security or bugs):
  - **Stored values use D** (split tags and payloads) wherever many values live together: the
    stack and table arrays, built in stages 5 and 6 and measured again there.
  - **A value in flight is A**, a tagged union, but **encapsulated**: the tag and the union are
    private, values are made only through typed constructors (`Value::integer(3)`), and every
    accessor checks the tag **in every build**, stopping the program on a mismatch, so reading
    the wrong member can never be silent undefined behavior. Measured with D storage, the
    checks cost nothing measurable (the compiler removes them where the tag was just tested),
    and the whole design ran within a few percent of raw D. It was faster than `std::variant`
    in flight, which has to be rebuilt from (tag, payload) on every load (adding mixed values:
    about 25 ms against 31.7 ms).
  - **No NaN-boxing**: D is as fast in the measurements without boxing big integers on the
    heap or normalizing NaNs, which are both places for bugs.
- **How operations report errors** (`1 // 0`, `nil + 1`). Options, measured in section 7:
  return `std::expected<Value, RuntimeError>`, or throw. The cost was the same.
  **Recommended: `std::expected`** for now, like the lexer and parser. The full answer, with
  `pcall`, is roadmap stage 10, and this stage only needs errors out of single operations.
- **Who owns strings.** **Recommended: like Lua.** A `Heap` creates every string and frees them
  all when it is destroyed, and values hold plain `String*`. The garbage collector (stage 9)
  later takes the `Heap`'s place. No `std::shared_ptr`: it doubles the pointer, adds a
  reference count to every copy, and leaks cycles once tables exist.
- **Interning and the hash.** **Decided: like Lua**: short strings (up to 40 bytes) interned,
  Lua's hash function (V10.3), and a random seed per `Heap`, so the order of `pairs` can change
  between runs, as in Lua. A `Heap` can also be given a fixed seed, which the tests use.- **String order.** **Recommended: byte order** (V8.4), which is what Lua's `strcoll` gives in
  the default C locale, on every platform.

## Where the code goes

```
lib/internal/value/value.hpp     Value (the tagged value), type names, truthiness
lib/internal/value/number.hpp    conversions and arithmetic on numbers
lib/internal/value/string.hpp    String and the Heap that owns strings
lib/internal/value/convert.hpp   strings to numbers and numbers to strings
lib/value/*.cpp                  their implementations
tests/value_test.cpp ...         tests, tagged [value]
```

Like the lexer and parser, values are internal to the library for now. The public embedding API
(`include/lua2/`) comes when there is a state to embed.

## Testing approach

Nothing can run Lua code yet, so every test is C++: build values, apply an operation, and check
the result's subtype and value, or the exact error message. Each test names its rule:

```cpp
TEST_CASE("V3.6 integer // rounds toward minus infinity", "[value]") {
    CHECK(floor_divide(integer(-7), integer(2)) == integer(-4));
}
```

Where Lua's own test suite checks a rule (`math.lua`, `strings.lua`), the test copies its cases.
Lua snippets used to check what real Lua does go into `tests/fixtures/my_lua/` as usual.

## Parts

### Part 0: Foundations

- Settle the decisions above.
- `Value` with `nil`, booleans, integers and floats (strings in part 5); the type name of a
  value; truthiness; a `debug_text(Value)` for test failure messages.
- **Rules**: V1.1-V1.5.

### Part 1: Integer and float conversions

- Integer to float; float to integer, exact only, and the `floor`/`ceil` variants that exact
  comparisons need (part 4) and `for` loops will need later.
- **Tests**: 2^53 and 2^53 + 1, `-0.0`, `2^63`, `-2^63`, `inf`, `nan`, `3.5`.
- **Rules**: V2.1-V2.4.

### Part 2: Arithmetic

- `+`, `-`, `*`, unary `-` with wraparound; `/` and `^` in float; `//` and `%` with floor
  semantics, their special cases and errors; the error for non-numbers.
- **Tests**: every row of V3, including `math.mininteger // -1`, `x % -1`,
  `-5.0 % math.huge`, `1 // 0.0`.
- **Rules**: V3.1-V3.10, V3.12.

### Part 3: Bitwise operations

- `&`, `|`, `~`, `<<`, `>>`, unary `~` on numbers; float to integer conversion and its error;
  logical shifts with large and negative displacements.
- **Rules**: V4.1, V4.2, V4.4, V4.5.

### Part 4: Equality and order

- `==` across types and subtypes; `<` and `<=` with exact integer/float comparison; `nan`;
  the two compare error messages.
- **Tests**: `math.maxinteger` against `2^63`, `2^53 + 1` against its float, `nan` against
  everything.
- **Rules**: V7.1-V7.3, V7.5, V7.6, V8.1-V8.3, V8.5, V8.6.

### Part 5: Strings

- `String` (header and bytes in one allocation), the `Heap` that owns them, interning of
  short strings, Lua's hash; strings in `Value`.
- Equality, byte order and length of strings.
- **Tests**: equal short strings are one object; long strings are not; `\0` inside strings;
  hashes of known strings with a fixed seed.
- **Rules**: V7.4, V8.4, V9.3, V10.1-V10.4.

### Part 6: Numbers to strings

- Integers in decimal; floats with `%.14g` (through `std::to_chars`) plus `.0`; `inf`, `-inf`,
  `nan`, `-nan`, `-0.0`.
- **Rules**: V6.1-V6.3.

### Part 7: Strings to numbers

- Reuse the lexer's numeral reading; add whitespace, sign, the whole-string check and the
  `n`/`N` rejection.
- **Tests**: every example of V5, plus every numeral form from the lexer tests.
- **Rules**: V5.1-V5.6.

### Part 8: Coercions, concatenation and length

- Strings in arithmetic, with the string library's error messages (bitwise operations never
  convert strings); `..` with numbers converted; the concatenate
  and length errors.
- **Rules**: V3.11, V4.3, V9.1, V9.2, V9.4.

### Part 9: Conformance

- Go through `math.lua` and `strings.lua` in the official suite and copy every assertion about
  the rules above that does not need the interpreter into C++ tests. Any surprise becomes a new
  test. Bump the version to 0.0.4. The values stage is done.

## How we work through a part

Same as before: you name the part, it is implemented with its tests, its section of
`docs/final_docs/values.md` is written, and nothing is committed until you approve it.
