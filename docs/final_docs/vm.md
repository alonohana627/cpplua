# VM and garbage collector

How lua2 runs Lua code and manages memory. This page describes the code as it is. The rules are
in [../spec/execution.md](../spec/execution.md), the background and measurements in
[../learn/vm_gc.md](../learn/vm_gc.md), and how the stage was planned in
[../plan/vm.md](../plan/vm.md).

**Status:** stage 5 is done (version 0.0.5). lua2 compiles Lua to Lua 5.4's bytecode and runs it:
locals, arithmetic, strings, control flow, functions, varargs, tail calls, closures and upvalues,
with a garbage collector in both of Lua's modes, and, ahead of their stages, globals and
`print`. Tables come in stage 6: until then the only table is the globals table, with string
keys only, and every table constructor fails. On the six benchmarks lua2 takes between 0.45 and
0.88 of the time of the real Lua 5.4.9.

## The pipeline

```
source text --Parser--> Ast --compile()--> Proto --load()--> LuaClosure on the stack
                                                                 |
                                         call() --> execute() (the VM) <--> Heap (memory, collector)
```

1. The **parser** (stage 3) turns source text into a tree.
2. The **compiler** (`vm/compiler.hpp`) turns the tree into a `Proto` per function, with Lua
   5.4's instructions: the same ones, in the same registers, with the same constants as the
   real Lua's compiler.
3. **`load()`** (`vm/run.hpp`) wraps the main function's proto in a closure, with `_ENV` as its
   one upvalue, and pushes it on the stack.
4. **`call()`** (`vm/execute.hpp`) starts the **VM**, which runs Lua functions, calling and
   returning between them without C++ recursion, until the called function returns.
5. Every object lives in the **`Heap`** (`gc/heap.hpp`), whose garbage collector frees what the
   program can no longer reach.

## Where it lives

| File                                 | Contents                                                       |
| ------------------------------------ | -------------------------------------------------------------- |
| `lib/internal/gc/object.hpp`         | `GcObject`, the header of every collectable object; colors, ages |
| `lib/internal/gc/pool.hpp`, `lib/gc/pool.cpp` | `Pool`: size-class pages for small objects           |
| `lib/internal/gc/heap.hpp`           | `Heap`: memory, the string table, the collector's state and API |
| `lib/gc/heap.cpp`                    | allocation, strings, object creation and freeing               |
| `lib/gc/collector.cpp`               | the collector, a port of `lgc.c`                               |
| `lib/internal/value/string.hpp`      | `String`, a collectable object                                 |
| `lib/internal/vm/proto.hpp`          | `Proto`, a compiled function; `list_proto()`, `chunk_id()`     |
| `lib/internal/vm/function.hpp`       | `LuaClosure` and `Upvalue`                                     |
| `lib/internal/vm/state.hpp`, `lib/vm/state.cpp` | `State` (a thread: the stack, frames, open upvalues) and `CallInfo` |
| `lib/internal/vm/compiler.hpp`, `lib/vm/compiler.cpp` | the compiler, a port of `lcode.c` and `lparser.c` |
| `lib/internal/vm/execute.hpp`, `lib/vm/execute.cpp` | `call()`, the dispatch, the cold paths    |
| `lib/vm/handlers.inc`                | the body of every instruction, written once                   |
| `lib/internal/vm/debug_info.hpp`, `lib/vm/debug_info.cpp` | names for error messages, from `ldebug.c` |
| `lib/internal/vm/run.hpp`, `lib/vm/run.cpp` | `load()`, `run()`, `display_text()`                     |
| `lib/internal/value/table.hpp`, `lib/value/table.cpp` | `Table`, for now only the globals table   |
| `lib/internal/std/base.hpp`, `lib/std/base.cpp` | `open_base()`: `print` (the standard library) |
| `include/lua2/interpreter.hpp`, `lib/interpreter.cpp` | `lua2::Interpreter`, the public entry   |
| `tests/vm_test.cpp`, `tests/gc_test.cpp`, `tests/my_lua_test.cpp` | tests: `[vm]`, `[gc]`, `[my_lua]` |

## Objects and the heap

### The object header

Every collectable object starts with a `GcObject` (Lua's `CommonHeader`): the link in the list
of all objects, its `ObjectType` (short string, long string, upvalue, proto, Lua closure,
thread, table), and the collector's `marked` byte. As in `lgc.h`, the low three bits of `marked` are the
object's **age** (generational mode) and then come two **white** bits, a **black** bit and a
finalization bit (stage 7). An object is *white* (not yet marked), *gray* (marked, but what it
refers to may not be) or *black* (marked, and everything it refers to too).

`Value` (stage 4) holds collectable objects by pointer, with tags from `Tag::ShortString` up;
`Value::as_object()` gives the header.

### Memory

`Pool` hands out small blocks (up to 512 bytes) from 64 KB pages, with a free list per 16-byte
size class, and bigger ones from `operator new`; measured 1.9 to 3.2 times faster than `malloc`
for the collector's pattern ([../learn/vm_gc.md](../learn/vm_gc.md)). Built with
AddressSanitizer, every block comes from `operator new` instead, so the sanitizer still sees a
use after free.

`Heap` is Lua's `global_State`. It counts memory the way Lua does: `total_bytes_` plus a **debt**,
the bytes allocated beyond what the collector allows. Every allocation (`Heap::allocate`) adds
to the debt; freeing subtracts. The stack, the frames, the string table's buckets and the
vectors inside protos are counted too. When the debt is positive, a collection step is due.

### Strings

`String` derives from `GcObject`; its bytes follow it in the same block. Short strings (up to 40
bytes) are interned in the string table: an array of buckets, a power of two long, doubled when
it holds as many strings as buckets and halved by the collector when under a quarter full. A
short string found in the table but already dead (the collector has not swept it yet) is brought
back to life, as Lua's `internshrstr` does.

### Protos, closures, upvalues

- **`Proto`** is a `GcObject` with `std::vector`s for its code, constants, nested protos, upvalue
  descriptions, line per instruction and locals. Their memory is counted when the compiler
  finishes it (`counted_bytes`) and given back when it dies.
- **`LuaClosure`** is a proto plus its upvalues, stored right after the closure in one block
  (`LuaClosure::size_for(n)`); `Value::closure()` makes a function value.
- **`Upvalue`** is a variable a closure shares with the function that declared it (E7). While
  that function runs, the upvalue is *open*: it points into the stack slot, and every closure of
  that variable goes through the same upvalue. When the slot goes away it is *closed*: the value
  moves into the upvalue itself. The stack keeps tags and payloads in two arrays, so an upvalue
  holds two pointers (`tag`, `payload`); open ones also keep their slot (`level`), so that when
  the stack moves they move too.

### The thread

`State` (Lua's `lua_State`) is the main thread, a `GcObject` the heap creates first and owns.
Its stack is two arrays (`stack_tags`, `stack_values`), one slot of each per Lua value, with
`extra_stack` (5) spare slots past `stack_last`; `top` is the first free slot. It starts with 40
slots and doubles when needed, up to 1,000,000 (`stack overflow`, E6.7), plus 200 more to handle
that error. Frames are `CallInfo`s in a linked list that is reused, so calls allocate nothing
once the list is long enough; they store **indices** into the stack, so moving the stack needs no
fixing there. Open upvalues form a list sorted by slot, highest first (`find_upvalue`,
`close_upvalues`).

## The collector

`collector.cpp` is a port of Lua 5.4's `lgc.c`, function by function; comments give Lua's names.
Weak tables and finalizers come with tables and metatables; their parts are left out.

**Roots.** Everything reachable from the main thread is alive: its stack up to `top`, its open
upvalues. Values above `top` are dead. Collection only happens at the VM's checks (CONCAT,
CLOSURE, growing the stack for a call), never inside an allocation, and the VM sets `top` to the
end of the live registers before a check, as Lua's `checkGC` does. C++ code can therefore hold
new objects without anchoring them, as long as it does not run the VM in between.

**Incremental mode** (the default, E9.5). A cycle goes through phases: *pause* (mark the roots),
*propagate* (traverse one gray object per step), *atomic* (finish marking in one go: the thread
again, upvalues of unmarked threads, everything modified during marking; then the current white
flips, so what is still white is dead), *sweep* (free dead objects, 100 per step, and whiten the
rest), then pause again. `step()` turns the debt into units of work with the step multiplier,
does at least a step's worth, and sets the next debt; at the end of a cycle the pause decides how
far memory may grow before the next (`set_pause`: 200% of what was alive). The atomic phase also
clears the dead part of the stack (so that no slot keeps a freed object) and shrinks the stack
when it is much bigger than needed.

**Write barriers.** While marking, a black object must not point to a white one. Upvalues are the
only objects the VM writes into (`SETUPVAL`, closing an upvalue), and both go through the forward
barrier (`luaC_barrier`), which marks the new value. The backward barrier, for tables, is ready
for stage 6. Without the upvalue barrier the `vm_barrier` fixture frees a live string (checked:
AddressSanitizer reports the use after free).

**Generational mode** (E9.5). Objects have ages: new, survival, old0, old1, old, touched1,
touched2. A *minor* collection marks only what can point to young objects and sweeps only the
young part of the list (`young_collection`), so that objects that survived two collections are
not visited again. When memory grows 100% past what the last *major* collection left, a major
one runs (a full cycle). If that frees less than half of the growth, it was a *bad* collection:
the collector runs incremental cycles until one sees fewer objects than the last by an eighth,
then goes back to minor collections (`step_generational_full`). Barriers make old objects that
receive young ones old0 (forward) or touched (backward), as in Lua.

**Control** (E9.6, until `collectgarbage` exists in stage 9): `collect()` (a full cycle),
`step()`, `set_mode()`, `stop()`/`restart()`/`is_running()`, `parameters()` (Lua's defaults:
pause 200, step multiplier 100, step size 2^13 bytes, minor multiplier 20, major multiplier 100),
`total_bytes()`, `cycles()`.

**Stress modes**, for testing: `GcStress::FullCycles` runs a whole cycle at every check, like
Lua's `HARDMEMTESTS`; `GcStress::Steps` runs one single step at every check, so marking is
spread over many instructions and barriers get exercised. Every `my_lua` program runs under
both, in both modes, with the same results.

## The compiler

`compile(heap, ast, source, chunk_name)` ports Lua's `lcode.c` and the code-generating half of
`lparser.c`, walking the tree instead of the tokens. The structures are Lua's: `FuncState` per
function, `BlockCnt` per block, expression descriptors (`ExpDesc`, Lua's `expdesc`) that delay
code generation until the use of a value is known, jump lists for `and`, `or` and comparisons,
registers handed out as a stack, the constants cache shared by all functions, constant folding
that refuses operations that would fail (E4.4), compile-time `<const>` constants that take no
register, `goto` and labels with `CLOSE` when leaving upvalue scopes, and the final pass
(`luaK_finish`) that marks returns of vararg functions and functions with upvalues to close.

The parser has already checked everything Lua checks while parsing, so only Lua's code-size
limits can fail here: "function or expression needs too many registers", "control structure too
long" (without Lua's "near" part, which needs the token).

**Same code as Lua.** `lua2 --bytecode file.lua` lists the code in `luac -l -l`'s format, and for
every file of the official Lua 5.4.9 test suite the listing is identical to the real `luac`'s
(memory addresses aside), down to the line of each instruction, with one exception: a call whose
table argument starts on the next line (`f\n{...}`) puts its `NEWTABLE` one line later. To get the
lines right, the tree records where each node ends (`Ast::end_line`), calls record the line of
their arguments, `if` clauses where `then` is, method calls their name, and functions where
`...` is; the compiler keeps Lua's "last line read" as it goes. The fixtures' `.bytecode` files
are `luac`'s listings, and the tests compare.

## The VM

### Dispatch

The VM's state is six values: the thread `state` (Lua's `L`), the next instruction `pc`, the
running function's registers as two pointers (`tags`, `vals`, its first slot in each array), its
constants `k`, and the instruction being run `i`. Every instruction is a function taking those six; at its end it
loads the next instruction and jumps to that instruction's function through a 128-entry table.
With `[[clang::musttail]]` and the `preserve_none` calling convention the six stay in registers
from one instruction to the next and the jump is a plain indirect jump: there is no loop, and
each instruction's jump is predicted on its own. This was the fastest of the styles measured in
[../learn/vm_gc.md](../learn/vm_gc.md), and it is again in the real VM:

| Benchmark       | tail calls |   switch |
| --------------- | ---------: | -------: |
| `closures`      |   114.4 ms | 118.1 ms |
| `fib`           |    69.7 ms |  73.4 ms |
| `loop_floats`   |   443.0 ms | 466.6 ms |
| `loop_integers` |   606.9 ms | 734.8 ms |
| `strings`       |   210.1 ms | 214.2 ms |
| `varargs`       |   172.9 ms | 197.5 ms |

(Release build, best of 5. The switch build beats the real Lua too, by less.)

The bodies are written once, in `handlers.inc`, with macros (`VM_HANDLER`, `VM_NEXT`, ...) that
`execute.cpp` defines for the dispatch style chosen at build time: `LUA2_DISPATCH=tailcall` (the
default with clang) or `switch` (a `switch` in a loop, for compilers without those attributes).

**Cold paths.** A handler that might call an ordinary function needs a stack frame, and pays for
it every time. So everything rare is a separate *cold handler* with the same six arguments,
entered with `VM_COLD(Name)` by a tail call: the slow path of comparisons, float `%` and `^`
(which call the C library), division by zero, calling a non-function, growing the stack, the
upvalue barrier, closing upvalues on return. The hot handlers (arithmetic, comparisons, jumps,
`CALL`, `RETURN0`, `RETURN1`, `FORLOOP`, `GETUPVAL`, ...) compile to straight code without a frame.
In the switch style the cold handlers are labels inside the switch.

### Arithmetic

Arithmetic instructions try the fast path and, when it works, skip the `MMBIN*` instruction that
follows; otherwise that instruction runs the slow path (Lua's `op_arith` and `luaT_trybinTM`). The
fast path decides with one compare on both tags at once (`tag_pair`), and loads each payload as
the type it is used as: a float goes from memory straight into a float register. (Loading it as
an integer and moving it across, as the first version did, made `loop_floats` 1.2 times slower
than Lua: it lengthens every chain of dependent float operations.)

The slow path computes with the stage 4 operations (`add`, `bit_and`, ...), which already handle
strings the way Lua's string library does. Its errors follow `luaT_trybinTM`: string arithmetic
keeps the string library's message; otherwise `luaG_opinterror` or `luaG_tointerror` name the
operand at fault.

### The globals table

Until stage 6 brings real tables, `Table` (`value/table.hpp`) is only what the globals need: a
hash of string keys to values, with open addressing, that doubles when three quarters full and
drops `nil` entries when it grows. The heap creates it with the main thread (`Heap::globals()`),
and it is a root of the collector, which marks its keys and values like Lua's `traversetable`.
Storing into it is a backward barrier (`barrier_back`), as `luaH_set` does: a black globals
table turns gray again, so a fresh string stored in it while the collector runs is not freed.
`load()` gives every chunk this table as its `_ENV` (E1.2), so a global `x` is `GETTABUP` /
`SETTABUP` on it, and `_ENV.x` is `GETFIELD` / `SETFIELD`. A key that is not a string is an error
for now (E8.4).

### C++ functions and `print`

A function written in C++ is a `CFunction`, `int (*)(State&)`: `Tag::CFunction` holds the pointer
itself, so it is not a collectable object (Lua's light C function). It finds its arguments from
`ci->func + 1` up to `top`, pushes its results and returns how many. `CALL` of anything but a
Lua closure goes to the cold handler `CallOther`, which either calls the C++ function
(`call_c`, Lua's `precallC` and `luaD_poscall`: a frame of its own, `min_stack` (20) free slots,
results moved to where the caller wants them) or raises "attempt to call a ... value".

`open_base()` (`std/base.cpp`) puts `print` in the globals table; `lua2::Interpreter` calls it.
`print` writes each argument as `display_text()` shows it, separated by tabs, then a newline
(Lua's `luaB_print`), through `Heap::write_output()`: stdout, flushed at each newline, unless
`Heap::set_output()` gives it somewhere else (the tests capture it that way).

### Calls and returns

`CALL` checks that the value is a Lua closure, then enters it (Lua's `luaD_precall`): the next
frame from the list, its function slot, its top, how many results the caller wants, its
constants; missing arguments become `nil`; the registers start right after the function, where
the caller put the arguments, so nothing is copied. If the stack is too small or no frame is
free, the cold path makes room (collecting first if a step is due, as `checkstackGCp` does) and
runs the `CALL` again. `RETURN1` and `RETURN0` do the work of `luaD_poscall` inline; `RETURN`
handles any count (`moveresults`), closing upvalues first when the compiler said so. The caller
resumes at the pc it saved, with the constants it saved in its frame. A frame entered from C++ is
*fresh*: returning from it leaves the VM.

`TAILCALL` moves the function and its arguments down to the current frame and runs it there, so
tail calls use no stack (E6.5). Vararg functions use Lua 5.4's layout: `VARARGPREP` moves the
function and its fixed parameters above the extra arguments, which stay where they were for
`VARARG` to copy; returning moves the frame back.

### Loops, closures, the rest

- `FORPREP` and `FORLOOP` are Lua's: an integer loop precomputes its iteration count, so it never
  overflows (E5.4); limits are clipped; a float loop counts in floats; numeric strings are
  converted.
- `TFORCALL` calls the iterator like `CALL` (in the VM, without recursion), and `TFORLOOP` tests the
  result.
- `CLOSURE` creates the closure, puts it in its register at once (so it is anchored), then finds
  each upvalue: an open upvalue of this frame's register, or one of this closure's upvalues.
- `CONCAT` joins a whole chain at once (`concatenate`, stage 4); `LEN` measures strings.
- The table instructions work on the globals table with string keys (see above); indexing
  anything that is not a table fails as in Lua, "attempt to index a nil value (local 't')".
  `NEWTABLE` says "tables are not supported yet" (E8.4).
- `TBC` and the closing value of a generic `for` accept only `nil` and `false` until metatables
  bring `__close`.

### Errors

A runtime error stops the VM with `Status::Error` and the message in `State::error_message`; no
C++ exceptions. `raise()` prefixes the position, `chunk:line:` (E8.1): the chunk name the way Lua
shows it (`chunk_id()`, Lua's `luaO_chunkid`) and the line of the instruction. `debug_info.cpp`
ports `ldebug.c`'s symbolic execution to name the value at fault (E8.2): a local still active at
that instruction, or the instruction that last set the register (an upvalue read, a constant, a
global or field access, a method lookup), giving "(local 'x')", "(upvalue 'x')", "(global
'x')", "(field 'x')", "(method 'x')", "(constant 'x')", and "(for iterator 'for iterator')" for a
generic `for`. Every message in the `err_*` fixtures is the real Lua's, word for word.

After an error, `run()` closes the upvalues of the dead frames and resets the stack and frames;
catching errors in Lua (`pcall`) comes with stage 8.

## Running chunks

- `load(L, source, chunk_name)` compiles a chunk and pushes its closure; `run(L, source, chunk_name)`
  loads and calls it and returns its results (E1.1). The chunk's source name is `"@" + chunk_name`,
  as Lua names a file.
- `display_text(value)` writes a value as `print` does (`luaL_tolstring`): numbers as V6 says,
  strings as they are, `true`, `nil`, and `function: 00007ff6929ebd90` with the address the way
  the C library's `%p` writes it (16 hex digits on Windows, `0x...` elsewhere).
- `lua2::Interpreter` (`include/lua2/interpreter.hpp`) is the public entry: `run(source,
  chunk_name)` returns the results as text or the error message; `collect()`,
  `set_generational()`, `memory_in_use()`.
- `lua2 file.lua` runs a file (its `print`s go to stdout), then prints what it returns, if
  anything, separated by tabs; `lua2 --generational file.lua` runs it in generational mode;
  errors go to stderr.

## Benchmarks

`bench/` holds six programs that only use what this stage builds; each returns its result, the
same under lua2 and the real Lua. `lua2_bench` takes the best of N runs of each. Release build,
best of 5, on the development machine:

| Benchmark       | lua2     | Real Lua 5.4.9 | Ratio |
| --------------- | -------: | -------------: | ----: |
| `closures`      | 114.4 ms |       234.1 ms | 0.49x |
| `fib`           |  69.7 ms |        80.8 ms | 0.86x |
| `loop_floats`   | 443.0 ms |       500.9 ms | 0.88x |
| `loop_integers` | 606.9 ms |       851.1 ms | 0.71x |
| `strings`       | 210.1 ms |       471.5 ms | 0.45x |
| `varargs`       | 172.9 ms |       236.3 ms | 0.73x |

Where the time goes:

- **Dispatch.** Tail calls with `preserve_none` keep the VM's state in registers; Lua's
  `luaV_execute` reloads `base` and tests `trap` for hooks at every instruction.
- **No hooks.** Lua checks for debug hooks on every instruction and every call; lua2 has no
  debug library yet. (When it comes, hooks will have to cost nothing when off.)
- **Allocation.** Closures, upvalues and strings come from the pool, not `malloc` (`closures`,
  `strings`).
- **Frameless hot paths.** Rare work is in cold handlers, so common instructions need no stack
  frame.

In generational mode lua2 is faster too (whole process, best of 3): `closures` 136 against 250
ms, `strings` 191 against 483, `fib` 87 against 99, `varargs` 190 against 249.

## Testing

- `tests/fixtures/my_lua/`: every Lua program written for the VM, with its golden output (`.out`:
  what it prints, then what it returns, from the real Lua), runtime error (`.runerror`, the real Lua's message) and bytecode
  (`.bytecode`, the real `luac`'s listing). `[my_lua]` runs them in both modes, then again under
  both stress modes; every `.lua` there that parses must also compile.
- `tests/vm_test.cpp` names the rules of `execution.md` one by one; `tests/gc_test.cpp` checks the
  collector's promises: unreachable objects die, reachable ones stay and do not move, the string
  table forgets dead strings, memory stays bounded, modes switch, a stopped collector does not
  collect, upvalues follow a moving stack.
- Everything passes under AddressSanitizer (`asan` preset), which also checks the collector,
  since the pool then gives each object its own allocation.

## AddressSanitizer

The `asan` preset is a debug build with `-fsanitize=address`. Sanitized code uses much bigger
stack frames, and the parser's 200-level nesting limit then overflowed Windows' default 1 MB
stack, so the preset links programs with an 8 MB stack.

## The reference Lua

`original_lua_src/` is the original Lua 5.4.9 distribution, unchanged: `lua-5.4.9.tar.gz` from
lua.org, extracted. It is only a reference, so `.gitignore` keeps it out of Git, and
`cmake/ReferenceLua.cmake` gets it when it is missing: at configure time it downloads the
archive, checks its SHA-256 (as on lua.org's download page:
`2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`),
extracts it next to the build and moves the folder into place. If the download fails or the
checksum differs, it warns and skips the reference, and lua2 builds as usual. It then builds
the source as `lua_ref` and `luac_ref` the way Lua's makefile does (C99, `-O2`,
`LUA_COMPAT_5_3`), without our warnings; the tidy target skips it. It is on by default
(`LUA2_REFERENCE_LUA`); turning it off also means nothing is downloaded.
