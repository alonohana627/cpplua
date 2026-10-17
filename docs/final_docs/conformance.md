# Conformance

**Status:** stage 11 is done (version 0.0.11): lua2 passes the official Lua 5.4.9 test suite.
`lua2 -e "_port=true" all.lua` in `tests/fixtures/lua-5.4.9-tests/` prints `final OK !!!`, in
every build. The rules are in [../spec/conformance.md](../spec/conformance.md), the plan in
[../plan/conformance.md](../plan/conformance.md), and the background in
[../learn/conformance.md](../learn/conformance.md).

## Running it

```sh
ctest --preset debug -R lua_5_4_test_suite       # as a test
cd tests/fixtures/lua-5.4.9-tests
../../../build/release/lua2 -e "_port=true" all.lua    # by hand: prints each file, then "final OK !!!"
../../../build/release/lua2 -e "_port=true" strings.lua   # one file
```

| Build | Time for `all.lua` |
| ----- | ------------------ |
| lua2 release | 3.0 s |
| the real Lua 5.4.9 (`lua_ref`, release) | 5.2 s |
| lua2 debug | 26.5 s |
| lua2 asan | about 2 minutes |

(Windows 11, one run each. The suite is not a benchmark: much of its time is the garbage
collector and string building, and the real Lua's includes its `longjmp`-based error
handling.)

`all.lua` writes `time.txt` in the folder when it finishes; Git ignores it.

## What the suite needed

**New features**, each with its own page:

- binary chunks, in Lua's exact format ([dump.md](dump.md), P2);
- the stand-alone program, a port of `lua.c` ([standalone.md](standalone.md), P3);
- the locale: string order, numerals and character classes after `os.setlocale` (P4);
- coroutines, hooks and the debug library (stage 10: [coroutines.md](coroutines.md),
  [debug.md](debug.md)).

**Bugs in earlier stages** the suite found, each fixed at its cause:

| Found by | Bug | Fix |
| -------- | --- | --- |
| `constructs.lua` | a loop calling only C++ functions never let the collector step (memory peaked at 179 MB) | a collector step after each C++ function returns, as `luaD_precall`'s `luaC_checkGC` |
| `errors.lua` | a message handler that overflowed the stack crashed instead of `error in error handling` | stack room computed signed (`free_slots`), and no message handler for an error in error handling (`LUA_ERRERR`) |
| `errors.lua` | tracebacks of very deep stacks took quadratic time | the depth is counted once |
| `errors.lua` | a light userdata argument was named `userdata` in messages | `light userdata`, as `luaL_typeerror` names it |
| `coroutine.lua` | closing a coroutine reported a later error instead of the first | each `__close` gets the error it was closed with; errors stay on the coroutine's stack, as in Lua |
| `coroutine.lua`, `db.lua` | line hooks off by one instruction, and missing for stripped functions | the previous instruction counted as Lua's `pcRel`; line hooks without line information as Lua sends them |
| `files.lua` | Windows text files: positions and newlines differed from the C library's | files opened in binary mode, `"\r\n"` read as `"\n"` and `"\n"` written as `"\r\n"` in text mode |
| `files.lua` | reading a write-only file (and writing a read-only one) did not fail | each file knows how it was opened: `Bad file descriptor`, as the C library answers |
| `files.lua` | a binary chunk after a `#` line did not load | the kept newline is dropped before a binary chunk |
| `literals.lua`, `strings.lua` | numerals, string order and character classes in another locale | P4 |

Every fix was compared with the real Lua on the same input first.

## Speed after stages 10 and 11

Coroutines, hooks and the rest cost nothing measurable: against a build of stage 9 (0.0.9),
every benchmark is within noise (0.94x to 1.02x) except `strings` (0.80x) and `closures`
(0.94x), faster because `lua2 file.lua` now runs the generational collector, as `lua` does.
Against the real Lua 5.4.9 (release, best of 5):

| Benchmark | lua2 | Lua 5.4.9 | Ratio |
| --------- | ---: | --------: | ----: |
| `closures` | 108.9 ms | 232.3 ms | 0.47x |
| `fib` | 67.2 ms | 82.7 ms | 0.81x |
| `lib_math` | 82.6 ms | 75.4 ms | 1.10x |
| `lib_string` | 23.5 ms | 23.5 ms | 1.00x |
| `lib_table` | 145.4 ms | 304.9 ms | 0.48x |
| `loop_floats` | 437.4 ms | 497.4 ms | 0.88x |
| `loop_integers` | 609.3 ms | 840.1 ms | 0.73x |
| `metamethods` | 96.6 ms | 124.0 ms | 0.78x |
| `methods` | 168.9 ms | 164.7 ms | 1.03x |
| `pcall` | 34.8 ms | 177.9 ms | 0.20x |
| `sort` | 114.4 ms | 137.5 ms | 0.83x |
| `strings` | 171.4 ms | 477.9 ms | 0.36x |
| `tables_array` | 49.1 ms | 75.9 ms | 0.65x |
| `tables_hash` | 111.0 ms | 107.6 ms | 1.03x |
| `varargs` | 171.0 ms | 235.0 ms | 0.73x |

`lib_math`, `methods` and `tables_hash` move between about 0.95x and 1.10x from run to run on
this machine (a second run gave 1.07x, 0.95x and 0.99x); the stage 9 build measures the same.

## Where it lives

| File | Contents |
| ---- | -------- |
| `tests/fixtures/lua-5.4.9-tests/` | the suite, unchanged from lua.org |
| `CMakeLists.txt` | the `lua_5_4_test_suite` test: passes when the output has `final OK !!!` |
| `lib/vm/dump.cpp`, `lib/standalone.cpp`, `lib/value/locale.cpp` | the new features |
