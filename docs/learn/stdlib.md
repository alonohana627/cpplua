# The standard library: background

The ideas behind roadmap stage 9, for reading the code. How lua2 does each is in
[../final_docs/stdlib.md](../final_docs/stdlib.md).

## 1. A library is a table of C functions

Lua's standard library is not part of the language: it is C code that uses the same API any
program embedding Lua would, and puts its functions in tables (`string`, `math`, ...). Lua the
language only knows how to call a C function: arguments in, results out, and an error raised
with a `longjmp`. So the library can be ported a file at a time, and lua2's is: each `.cpp` in
`lib/std/` follows its `.c` file function by function. What changes is the error path: lua2 has
no `longjmp`, so every check returns false after raising its error and the function returns
`c_function_error`.

## 2. Lua patterns are not regular expressions

`string.find`, `match`, `gmatch` and `gsub` use Lua's patterns: character classes (`%a`, `%d`,
`[a-z]`), the repetitions `*`, `+`, `-` (lazy) and `?` on single classes only, anchors, captures
(also of positions, `()`), back references (`%1`), balanced matches (`%b()`) and frontiers
(`%f[%w]`). There is no alternation and no repetition of groups, which keeps the matcher tiny: a
recursive backtracking function of a few hundred lines that walks the pattern and the subject
together. Each repetition tries the longest (or, for `-`, the shortest) run first and backs off
one character at a time. Recursion is bounded (200 levels), which is the `pattern too complex`
error. A pattern with none of the special characters `^$*+?.([%-` is plain text, so `find`
searches it with a plain substring search.

## 3. `string.format` is `printf`

`string.format` walks its format string and hands each conversion (`%5.2f`, `%x`, ...) to the C
library's `printf` with the next argument, after checking it against what Lua allows (flags,
at most two digits of width and precision). So its output is the C library's, which is why
`nan` and `%a` used to differ between systems: lua2 uses MinGW's own C99 `printf` on Windows,
which writes them as other systems do. `%q` is Lua's own: a string quoted so that reading it back
gives the same string, integers in decimal, floats in hexadecimal so no digit is lost.

## 4. `string.pack` describes C structures

`pack` and `unpack` turn values into bytes and back by a format like `<i4 d s1`: endianness,
integers of 1 to 16 bytes, floats, strings with a length prefix or a terminating zero, and
alignment padding (`!`), like the layout of a C `struct`. It is how Lua reads and writes binary
files and network formats.

## 5. Random numbers: xoshiro256\*\*

Lua 5.4's `math.random` is xoshiro256\*\*, a generator with 256 bits of state, a period of
`2^256 - 1`, and good statistical quality for very little work (shifts, rotations, xors, two
multiplications). `math.randomseed(n)` sets the state from `n` and discards 16 values, so lua2
gives exactly the numbers the real Lua gives for a seed. A random integer in `[m, n]` is
`project`ed without bias: it takes the smallest mask `2^b - 1` that covers the range and draws
again while the masked value falls outside, instead of using `%`, which would favor small
numbers.

## 6. Sorting: Lua's quicksort

`table.sort` is a quicksort with median-of-three pivots, the smaller half sorted recursively and
the larger one by a loop, so the stack stays logarithmic. When a partition comes out very
unbalanced, later pivots are chosen at random (from the clock), which defeats inputs built to
make quicksort quadratic. An order function that is not a strict order (`<=` instead of `<`) can
make the partition loop run off the array: that is `invalid order function for sorting`. The
sort is not stable: equal elements may change places, and lua2 runs the same steps so they
change places the same way.

## 7. Files are userdata

Lua's `io` wraps the C library's `FILE*` in a full userdata with a metatable (`__index` holds
the methods, `__gc` and `__close` close the file, `__name` is `FILE*`); lua2's wraps a C++
`std::filebuf` the same way, the lower layer of `std::fstream`, which reads and writes bytes
with a buffer much as a `FILE*` does. The standard streams are
files too, whose close function refuses to close them. `io.lines` returns an iterator closure
that keeps the file and the read formats as upvalues and closes the file at the end when it
opened it, and since Lua 5.4 also returns the file as a to-be-closed value, so a `break` out of
the loop closes it.

## 8. `require` is a search

`require(name)` returns `package.loaded[name]` if it is there; otherwise it asks each function
of `package.searchers` in turn for a loader: `package.preload[name]`, then a Lua file found on
`package.path` (each `?` in the `;`-separated templates replaced by the name, with dots as
directory separators), then C libraries on `package.cpath`. The loader runs with the name and
where it was found, and its result (or `true`) is stored in `package.loaded`, so the module runs
once. When nothing is found, the error lists everything that was tried.
