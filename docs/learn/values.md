# Values: what a Lua value is, and how to hold one in C++

Reading material for roadmap stage 4. Nothing here is code we have written yet. The exact rules
are in [../spec/values.md](../spec/values.md); this page explains the ideas behind them and the
choices we have to make.

## 1. Dynamic typing

In C++ a variable has a type, fixed when the program is compiled. In Lua only **values** have
types: a variable can hold `1` now and `"one"` later, and `x + y` decides what to do when it
runs, by looking at what `x` and `y` hold.

So the interpreter needs one C++ type, call it `Value`, that can hold any Lua value, and knows
which kind it holds. Everything else (variables, table slots, function arguments, return values)
is made of `Value`s. That makes `Value` the most copied and most inspected type in the whole
interpreter: its size and the cost of asking "what are you?" show up in every benchmark later.

## 2. How Lua does it: tagged values

Lua's C code (`lobject.h`) uses a **tagged value**: a small tag that says the type, next to a
union that holds the payload.

```c
typedef union Value {
  struct GCObject *gc;   /* strings, tables, functions, ... */
  void *p;               /* light userdata */
  lua_CFunction f;       /* light C functions */
  lua_Integer i;         /* integer numbers */
  lua_Number n;          /* float numbers */
} Value;

typedef struct TValue {
  Value value_;
  lu_byte tt_;           /* the tag */
} TValue;
```

The payload is 8 bytes and the tag 1, so with padding a `TValue` is 16 bytes. Small values
(`nil`, booleans, numbers) live entirely inside it. Big values (strings, tables, functions) live
on the heap, and the `TValue` holds a pointer to them; copying the `TValue` copies the pointer,
so two variables can refer to the same table.

The tag also encodes the *variant*: integer and float are two variants of the type `number`,
just as short and long strings are two variants of `string`. That is how Lua 5.4 can say
"`math.type(1)` is `integer`" while `type(1)` is still `number`.

## 3. Three ways to write it in C++

All three hold the same values. They differ in size, in how a `switch` over the kind looks, and
in how much the compiler checks for us.

### A. A tagged union, like Lua's

```cpp
enum class Tag : std::uint8_t { Nil, False, True, Integer, Float, String };

struct Value {
    Tag tag;
    union { std::int64_t integer; double number; String* string; };
};
```

Exactly Lua's layout. Every access is "check the tag, then read the matching union member", and
reading the wrong member is undefined behavior that nothing catches.

### B. `std::variant`

```cpp
struct Nil {};
using Value = std::variant<Nil, bool, std::int64_t, double, String*>;
```

The same 16 bytes: libc++ stores the index in one byte next to the 8-byte payload, just like
the tag. The difference is safety: `std::get_if<double>(&v)` returns `nullptr` instead of
reading the wrong member, and `std::visit` with one lambda per alternative fails to compile when
an alternative is forgotten. That is the same reason the AST is made of variants.

### C. NaN-boxing

A double has 2^52 different NaN bit patterns, and real arithmetic only ever produces one of
them. NaN-boxing hides every other kind of value inside the unused NaN patterns, so a value is
one 8-byte word: if it is not one of "our" NaNs, it is a float; otherwise some bits say the kind
and the remaining 48 bits hold a pointer or a small integer. LuaJIT and most JavaScript engines
do this.

It halves the size of every value. But it has room for only about 48 bits of payload, and
**Lua 5.4 integers are 64 bits** (`math.maxinteger` is 2^63 - 1, and integer overflow wraps
around at 64 bits, V3.5). So a NaN-boxed Lua 5.4 must store bigger integers somewhere else, on
the heap, and check for that on every integer operation. LuaJIT avoids the problem because it
implements Lua 5.1, which has no integers.

There is no way around that with 8 bytes and no heap: there are 2^64 integers and almost 2^64
doubles, and one 64-bit word has room for only 2^64 values in total, before any tag. Every
8-byte design gives something up:

- **C2. NaN-boxing with boxed big integers.** Integers that fit in 48 bits are stored inline;
  bigger ones live in a heap object, and the value points to it. Correct for all 64 bits, and
  small integers stay fast. The costs: a range check on every integer result; an allocation
  (and later garbage) for every big integer, which matters for code that works with full
  64-bit numbers, such as hash functions, bit tricks, `math.maxinteger` or `math.random`'s
  64-bit results; boxed integers compared by value, not by bits; every NaN that enters from
  outside (`string.unpack`, the C API) normalized so it cannot look like a tag; and pointers
  that must fit in 48 bits.
- **Smaller inline integers the other way round**: box the doubles instead and keep 63-bit
  integers inline (OCaml's choice). Worse for Lua, where floats are common and integers still
  need all 64 bits.
- **D. Tags apart from payloads**: keep 8-byte payloads in one array and 1-byte tags in
  another. A *stored* value then costs 9 bytes instead of 16, with all 64 bits. This only
  works where values sit in arrays (the VM's stack, a table's array part), not for a single
  value passed around. Lua 5.5 does this for table arrays.

### Measured

A small benchmark (`clang++ -O2`, libc++, 4 million values, best of 15 runs): adding and
comparing pairs of values from two arrays with Lua's rules, and summing an array through values
(`s = s + a[i]`). C skips the 64-bit fallback (a best case that would fail the test suite); C2
has it. The values in the benchmark are small, so C2 never actually boxes: it measures the cost
of the checks only. D keeps 8-byte payloads and 1-byte tags in two separate arrays.

| Representation               | Size | add, mixed | less, mixed | sum, mixed | add, integers | less, integers | sum, integers |
| ---------------------------- | ---: | ---------: | ----------: | ---------: | ------------: | -------------: | ------------: |
| A. tagged union              | 16 B |   34.5 ms  |    32.9 ms  |   29.6 ms  |      16.9 ms  |        6.9 ms  |      26.5 ms  |
| B. `std::variant` + `get_if` | 16 B |   25.9 ms  |    25.8 ms  |   24.3 ms  |      17.3 ms  |        4.6 ms  |      26.1 ms  |
| B. `std::variant` + `visit`  | 16 B |   26.2 ms  |    26.7 ms  |   38.4 ms  |      18.4 ms  |        6.4 ms  |      26.7 ms  |
| C. NaN-boxing, 48-bit only   |  8 B |   22.7 ms  |    21.1 ms  |   12.3 ms  |       3.4 ms  |        2.5 ms  |       2.7 ms  |
| C2. NaN-boxing, 64-bit       |  8 B |   25.0 ms  |    24.5 ms  |   14.2 ms  |       4.5 ms  |        4.2 ms  |       5.4 ms  |
| D. split tags and payloads   |  9 B |   23.4 ms  |    20.3 ms  |   11.6 ms  |       4.4 ms  |        2.5 ms  |       1.6 ms  |

What it says:

- A and B are the same size and close in speed. `std::variant` with `get_if` was as fast as or
  faster than the hand-written union almost everywhere, so the safety costs nothing.
- The 16-byte forms are 4 to 16 times slower on integers than the compact ones. Part of it is
  moving twice the bytes; the rest is that the compiler vectorizes the compact loops and not
  the 16-byte ones.
- **D is as fast as NaN-boxing, without its costs**: all 64 bits, no boxing, no allocation, no
  NaN normalization. Its limit is that it is a way to *store* values, not a type for one value:
  a value being passed around is still a (tag, payload) pair, which lives in two registers.
- D needs a value in flight that converts cheaply to and from (tag, payload). A
  `std::variant` does not: it must be rebuilt with a `switch` on every load. Measured with D
  storage, a tagged union in flight added mixed values in 23.4 ms and a variant in 31.8 ms
  (sums: 11.5 against 15.2 ms).
- The encapsulated version planned for lua2 (D storage; a value class with a private tag and
  union, whose accessors check the tag and stop the program on a mismatch) was measured too,
  with the checks compiled out and with them always on:

  | 4 million values              | add, mixed | less, mixed | sum, mixed | add, integers | less, integers | sum, integers |
  | ----------------------------- | ---------: | ----------: | ---------: | ------------: | -------------: | ------------: |
  | D. raw split arrays           |   23.4 ms  |    20.3 ms  |   11.8 ms  |       4.4 ms  |        2.5 ms  |       1.7 ms  |
  | Encapsulated, checks off      |   25.9 ms  |    20.3 ms  |   11.6 ms  |       4.9 ms  |        2.6 ms  |       1.7 ms  |
  | Encapsulated, checks always on|   25.2 ms  |    21.0 ms  |   11.5 ms  |       4.7 ms  |        2.6 ms  |       1.5 ms  |

  The checks are free: they repeat a test the operation just made, and the compiler removes
  the repeat. The encapsulated class is within noise of raw D except mixed addition, about 8%
  slower in three runs out of three.
- These are loops over arrays, not an interpreter. They say how values should be stored where
  there are many of them (the stack, table arrays), which arrives in stages 5 and 6. They will
  be measured again there in the VM (stage 5).
- An earlier version of this benchmark ran `s = s + 1`, and the compiler turned some of those
  loops into a single multiplication. Micro-benchmarks measure the compiler as much as the
  design.
## 4. Integers and floats

Lua 5.3 split numbers into two subtypes, and Lua 5.4 keeps them:

- **Integers** are exact, 64-bit and wrap around on overflow (V3.5): `math.maxinteger + 1` is
  `math.mininteger`, not an error and not a float. In C++, signed overflow is undefined
  behavior, so the arithmetic must be done on `std::uint64_t` and converted back, as Lua's
  `intop` macro does.
- **Floats** are doubles, with `inf`, `nan` and `-0.0`.

The rules of V3 decide which one an operation produces. The two that surprise people:

- `/` is always float division: `4 / 2` is `2.0`. For an integer result there is `//`.
- `//` and `%` **floor**: they round toward minus infinity, while C++'s `/` and `%` truncate
  toward zero. `-7 // 2` is `-4` in Lua and `-7 / 2` is `-3` in C++. Lua fixes up the C result
  when the signs differ and the division was not exact:

  ```cpp
  std::int64_t q = a / b;
  if ((a % b != 0) && ((a ^ b) < 0)) {   // signs differ: C rounded the wrong way
      q -= 1;
  }
  ```

  Two more corner cases: dividing by `0` is an error (V3.8), and `math.mininteger // -1`
  overflows in C++ (undefined behavior, and a crash on x86), so `b == -1` is handled
  separately as `0 - a`, which wraps.

### Comparing an integer with a float

`1 == 1.0` is true and `1 < 1.5` is true: numbers compare by mathematical value (V7.2, V8.2).
The obvious code converts the integer to a float and compares two floats. That is wrong for big
integers, because a double has only 53 bits of mantissa:

```lua
math.maxinteger + 0.0 == 2^63     -- true: the conversion rounds up
math.maxinteger == 2^63           -- must be false: 2^63 - 1 is not 2^63
```

Lua compares exactly: when the integer fits in 53 bits the float comparison is exact anyway;
otherwise it converts the *float* to an integer, rounding in the safe direction (`floor` or
`ceil`), and compares integers. Floats outside the integer range and `nan` are handled first.

## 5. Strings

A Lua string is an immutable sequence of bytes (V10.1). Immutable means a string can be shared
freely: copying a `Value` that holds a string copies a pointer, never the bytes.

**Interning.** Lua keeps one copy of each short string (up to 40 bytes) in a hash table inside
the state (V10.2). Creating `"name"` twice gives the same object both times. Two payoffs:

- Equality of short strings is one pointer comparison, and table lookups by string keys (field
  names, global names: almost every lookup) hash once, when the string is created.
- Repeated strings use memory once.

Long strings are not interned: hashing a 1 MB string just to look it up would cost more than it
saves. They are compared by length and bytes.

**The string object.** Lua allocates the header and the bytes in one block:

```cpp
struct String {
    std::uint32_t hash;
    std::uint32_t length;      // Lua uses size_t; 4 GB strings are not a goal here
    bool is_short;
    char bytes[];              // `length` bytes, then a '\0' for C interop
};
```

One allocation per string, and the bytes sit right after the header in memory. C++ has no
flexible array members in the standard (`char bytes[]` is a compiler extension), so the
portable form allocates `sizeof(String) + length + 1` bytes and finds the bytes just past the
header.

**Who owns strings before there is a garbage collector?** The garbage collector is stage 5, together with the VM.
Until then the simplest correct owner is a `Heap` (later the GC) that records every object it
creates and frees them all when it is destroyed. Values hold plain `String*` pointers, never
owning ones. That is what the GC will want too: it, not the values, decides when an object
dies. `std::shared_ptr<String>` would make every `Value` copy touch a reference count, double
the pointer size, and leak cycles once tables can point at each other.

## 6. Conversions between strings and numbers

**String to number** (V5) happens in arithmetic (`"10" + 1`) and later in `tonumber`. The
syntax is exactly that of a numeral in source code, plus surrounding whitespace and a sign. Our
lexer already reads numerals, including hex floats, overflow to float and hex wraparound. The
conversion should reuse that code, not grow a second numeral reader; Lua does the same in
`luaO_str2num`.

**Number to string** (V6) uses `%.14g` for floats, then adds `.0` when the text looks like an
integer. 14 digits is less than a double needs to round-trip exactly (17), so `0.1 + 0.2`
prints as `0.3` even though it is not 0.3. That is a deliberate Lua choice, and print output in
the test suite depends on it. C++ offers the same format without `printf`:
`std::to_chars(first, last, value, std::chars_format::general, 14)` gives exactly `%.14g`.

## 7. When an operation fails

`1 // 0` and `nil + 1` are runtime errors. The VM (stage 5) will run Lua code that runs
these operations, and `pcall` (stage 9) must be able to catch the error and continue. Two ways
to carry the error out of an operation:

- **Return it**: every operation returns `std::expected<Value, RuntimeError>`, the same style as
  the lexer and parser. Every caller checks the result and passes errors up.
- **Throw it**: operations return a plain `Value` and throw on error; `pcall` becomes a
  `try`/`catch`. This is the C++ version of what Lua does with `longjmp`.

Measured (4 million additions, then 4 million integer divisions, error path never taken):

| Style           | additions | divisions |
| --------------- | --------: | --------: |
| `std::expected` |   17.2 ms |   20.2 ms |
| exceptions      |   17.9 ms |   20.2 ms |

No difference worth deciding on. So it is a design question: `std::expected` keeps every error
path visible in the code and matches the rest of lua2; exceptions make deep interpreter code
shorter and map directly onto `pcall`. Stage 4 only needs errors to come out of single
operations, and the roadmap already puts the full answer in stage 9.

## 8. What this stage builds

Values exist before anything can run Lua code, so stage 4 is tested from C++: build values,
apply operations, check results and error messages against the rules in the spec. When the
interpreter arrives, the same operations become the meaning of `+`, `==`, `..` and the rest,
and the official test suite (`math.lua`, `strings.lua`) checks them again from Lua.
