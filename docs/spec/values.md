# Lua 5.4 values

The rules our values implement: types, numbers, arithmetic, bitwise operations, conversions,
comparisons, concatenation and length. It is based on the Lua 5.4 Reference Manual §2.1 (values
and types), §3.4.1-§3.4.7 (operators) and §6.4 (the `%.14g` number format), restated in our own
words. Where the manual is silent, the reference implementation (`lvm.c`, `lobject.c`,
`lstring.c` in Lua 5.4) is the tie-breaker, and those rules are marked **(impl)**.

Every rule has an id like `V3.6`. Tests should name the rule they check, for example
`TEST_CASE("V3.6 integer // rounds toward minus infinity", "[value]")`.

Error messages here are the core of Lua's message. When the interpreter exists, it adds where
the value came from, as in `attempt to perform arithmetic on a nil value (global 'x')`; that
part is not a value rule.

---

## V1. Types

- **V1.1** Lua has eight types: `nil`, `boolean`, `number`, `string`, `function`, `userdata`,
  `thread` and `table`. Stage 4 builds the first four; the others arrive with their stages.
- **V1.2** The type name of a value is one of those eight words exactly (what `type(v)`
  returns, and what error messages say: `a nil value`, `a string value`).
- **V1.3** `number` has two subtypes, **integer** and **float**. A value is always exactly one
  of them, and the program can tell which (`math.type(v)` is `"integer"` or `"float"`). The
  subtype matters for arithmetic results, division, conversion to text and table keys; most
  other rules compare numbers by their mathematical value.
- **V1.4** `nil` and `false` are *falsy*; every other value is *truthy*, including `0`, `0.0`
  and `""`. `and`, `or`, `not`, `if` and `while` test truthiness.
- **V1.5** `nil` is a type with one value; `boolean` has two, `true` and `false`.

## V2. Integers and floats

- **V2.1** Integers are 64-bit two's complement (`math.mininteger` = -2^63,
  `math.maxinteger` = 2^63 - 1).
- **V2.2** Floats are IEEE 754 doubles, with `inf`, `-inf`, `nan` and `-0.0`.
- **V2.3** Integer to float: the nearest double (exact up to 2^53, rounded beyond).
- **V2.4** Float to integer, where an integer is required (bitwise operations, V4.2): only a
  float with an exact integer value in range converts: `3.0` becomes `3`, but `3.5`, `2^63`,
  `inf` and `nan` do not. **(impl: `luaV_flttointns` with `F2Ieq`)**
- **V2.5** A numeral in source code is an integer if it has no `.` and no exponent and fits;
  otherwise a float (lexical rules L5, already done in the lexer).

## V3. Arithmetic

```
V3.1   +  -  *  /  //  %  ^  and unary -
```

- **V3.2** For `+`, `-`, `*`, `//`, `%` and unary `-`: if both operands are integers, the
  result is an integer.
- **V3.3** `/` and `^` always convert both operands to float and give a float: `4 / 2` is
  `2.0`, `2 ^ 2` is `4.0`.
- **V3.4** Otherwise, when either operand is a float, both are converted to float (V2.3) and
  the operation is done in floating point: `1 + 2.0` is `3.0`.
- **V3.5** Integer operations wrap around on overflow, modulo 2^64:
  `math.maxinteger + 1 == math.mininteger`, and `-math.mininteger == math.mininteger`.
- **V3.6** Integer `//` is floor division, rounding toward minus infinity: `7 // 2 == 3`,
  `-7 // 2 == -4`, `7 // -2 == -4`. `math.mininteger // -1` wraps to `math.mininteger`.
  **(impl)**
- **V3.7** Integer `%` has the sign of the divisor: `a % b == a - (a // b) * b`. So
  `5 % 3 == 2`, `-5 % 3 == 1`, `5 % -3 == -1`, and `x % -1 == 0` for every `x`. **(impl)**
- **V3.8** Integer `//` or `%` by zero is an error: `attempt to divide by zero` and
  `attempt to perform 'n%0'`. **(impl)**
- **V3.9** Float `//` is `floor(a / b)`. Float `%` is `a - floor(a / b) * b`, computed as
  `fmod(a, b)` and then moved by `b` when its sign differs from `b`'s: `5.5 % 2 == 1.5`,
  `-5.5 % 2 == 0.5`, `5.0 % math.huge == 5.0`, `-5.0 % math.huge == math.huge`. Float division
  by zero is not an error: `1 / 0 == inf`, `0 / 0` is `nan`, `1 // 0.0 == inf`,
  `1 % 0.0` is `nan`. **(impl: `luai_numidiv`, `luai_nummod`)**
- **V3.10** `^` is `pow(a, b)` on floats.
- **V3.11** Strings that look like numbers take part in arithmetic after conversion (V5):
  `"10" + 1 == 11`, `"3.0" + 1 == 4.0`, `"0x10" * 1 == 16`. **(impl: this is not in the core
  but in the string library's metamethods, `__add` and friends in `lstrlib.c`; the standard
  `lua` program always has them)** When a string takes part and the other operand cannot be
  converted, the error comes from the string library: `attempt to add a 'string' with a 'nil'`,
  with the metamethod's name (`add`, `sub`, `mul`, `mod`, `pow`, `div`, `idiv`, `unm`) and
  both operands' type names in order **(impl: `trymt`)**.
- **V3.12** Any other operand is an error: `attempt to perform arithmetic on a X value`, with
  `X` the type name (V1.2) of the first operand if it is not a number, otherwise of the second.
  **(impl: `luaG_opinterror`)**

## V4. Bitwise operations

```
V4.1   &  |  ~  <<  >>  and unary ~
```

- **V4.2** Both operands are converted to integers (V2.4), and the result is always an
  integer: `3.0 | 0 == 3`. When both operands are numbers but one is a float with no exact
  integer value, the error is `number has no integer representation` **(impl:
  `luaG_tointerror`)**.
- **V4.3** Strings are **not** converted: `"3" & 1` is the error
  `attempt to perform bitwise operation on a string value`. The string library has no bitwise
  metamethods. (The official test suite's `bitwise.lua` adds them itself, from
  `bwcoercion.lua`.) **(impl: `luaV_tointegerns`, `lstrlib.c`)**
- **V4.4** `<<` and `>>` are logical shifts: vacated bits are zeros, also for `>>` on negative
  numbers. A displacement of 64 or more (either way) gives `0`; a negative displacement shifts
  the other way: `1 << -1 == 0`, `2 >> -1 == 4`, `-1 >> 1 == math.maxinteger`.
- **V4.5** When an operand is not a number, the error is
  `attempt to perform bitwise operation on a X value`, naming the first operand if it is not a
  number, otherwise the second (as V3.12). This check comes first: `1.5 & nil` names `nil`.
  **(impl: `luaT_trybinTM`)**

## V5. Strings to numbers

The rules for converting a string to a number (arithmetic, bitwise operations, and later
`tonumber` without a base). **(impl: `luaO_str2num`)**

- **V5.1** The string, without leading and trailing whitespace (space, `\t`, `\n`, `\v`, `\f`,
  `\r`), must be one whole numeral: `" 10 "` converts, `"10x"` and `""` do not.
- **V5.2** A leading `-` or `+` is allowed: `"-0x10"` is `-16`.
- **V5.3** It is an integer when it reads as an integer numeral that fits (`"10"`, `"0x10"`).
  Hex integers wrap around (`"0xffffffffffffffff"` is `-1`), as in the lexer. A decimal integer
  that does not fit becomes a float: `"9223372036854775808"` is `2^63` as a float.
- **V5.4** Otherwise it is a float if it reads as a decimal or hex float numeral (`"1e2"`,
  `".5"`, `"0x1p4"`). `inf`, `nan` and anything containing `n` or `N` are never accepted, even
  though C's `strtod` would accept them.
- **V5.5** A string with a `\0` byte inside never converts.
- **V5.6** The number keeps the subtype it read as: `"10" + 0` is the integer `10`,
  `"10.0" + 0` the float `10.0`.

## V6. Numbers to strings

The text of a number, used by concatenation (V9) and later by `tostring` and `print`.
**(impl: `tostringbuff` in `lobject.c`)**

- **V6.1** An integer is written in decimal: `10`, `-3`, `-9223372036854775808`.
- **V6.2** A float is written with `printf`'s `%.14g` (14 significant digits). If the result
  looks like an integer (only digits and `-`), `.0` is added: `1.0`, `-0.0`, `1e+15`
  (unchanged: it has `e` and `+`), `0.1`, `3.1415926535898`, `1e+100`.
- **V6.3** `inf` and `-inf` are written as `inf` and `-inf`; a NaN as `nan` or `-nan`,
  following its sign bit **(impl: what glibc's `printf` prints; we fix it to this on every
  platform)**.

## V7. Equality

- **V7.1** `==` first compares types: values of different types are never equal (`"1" == 1`
  is false; strings never convert for equality).
- **V7.2** Numbers are equal when they have the same mathematical value, whatever their
  subtypes: `1 == 1.0`, `2^53 == 2^53 + 0.0`. The comparison is exact:
  `math.maxinteger == math.maxinteger + 0.0` is false, because the float rounds to 2^63.
  **(impl)**
- **V7.3** `nan` is not equal to anything, itself included.
- **V7.4** Strings are equal when they have the same bytes.
- **V7.5** Booleans and nil compare by value. Tables, functions, userdata and threads compare
  by identity (with `__eq` for tables and userdata, stage 7).
- **V7.6** `~=` is the negation of `==`.

## V8. Order

- **V8.1** `<`, `<=`, `>` and `>=` work on two numbers or two strings. `a > b` is `b < a`, and
  `a >= b` is `b <= a`.
- **V8.2** Numbers compare by mathematical value, exactly, whatever their subtypes:
  `1 < 1.5`, `math.maxinteger < 2^63`, `math.maxinteger + 0.0 == 2^63` but
  `math.maxinteger < math.maxinteger + 0.0`. **(impl: `LTintfloat`, `LEintfloat`)**
- **V8.3** Any comparison with `nan` is false, including `nan <= nan`.
- **V8.4** Strings compare byte by byte, as unsigned bytes; a string that is a prefix of
  another is smaller: `"a" < "b"`, `"Z" < "a"`, `"" < "a"`, `"a" < "a\0"`. **(impl: Lua uses
  `strcoll`, which in the default C locale is this byte order; we always use byte order)**
- **V8.5** Strings and numbers never convert for order: `1 < "2"` is an error.
- **V8.6** Anything else is an error (until metamethods, stage 7):
  `attempt to compare two X values` when both have the same type name, otherwise
  `attempt to compare X with Y`. The order is the one of the `<` or `<=` actually performed
  (V8.1), so `1 > nil` is `attempt to compare nil with number`. **(impl: `luaG_ordererror`)**

## V9. Concatenation and length

- **V9.1** `..` joins two strings. Numbers are converted to strings first (V6):
  `1 .. 2 == "12"`, `1.0 .. "" == "1.0"`, `-0.0 .. "" == "-0.0"`.
- **V9.2** Any other operand is an error: `attempt to concatenate a X value`, naming the
  first operand that is neither a string nor a number. **(impl)**
- **V9.3** The length `#s` of a string is its number of bytes: `#"" == 0`, `#"a\0b" == 3`,
  `#"é" == 2` in UTF-8.
- **V9.4** The length of anything but a string or a table is an error:
  `attempt to get length of a X value`.

## V10. Strings

- **V10.1** A string is an immutable sequence of bytes. Any byte may appear, `\0` included, and
  there is no encoding: `"é"` is just two bytes.
- **V10.2** Strings of up to 40 bytes are *short*: Lua keeps exactly one copy of each
  (*interning*), so equal short strings are the same object. Longer strings are created anew
  each time. **(impl: `LUAI_MAXSHORTLEN`, `luaS_newlstr`)**
- **V10.3** A string's hash is computed from its bytes and a per-state random seed:
  `h = seed ^ length`, then for each byte from last to first,
  `h ^= (h << 5) + (h >> 2) + byte`. Short strings hash on creation; long strings only when
  first used as a table key. **(impl: `luaS_hash`)**
- **V10.4** The interning and the hash are not visible to a Lua program, except through speed
  and through the order of `pairs` (which Lua leaves unspecified). They are listed here because
  we follow them.
