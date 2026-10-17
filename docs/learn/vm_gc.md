# Running Lua fast: a bytecode VM and a garbage collector

Reading material for roadmap stage 5. Nothing here is code we have written yet. What running a
Lua program must do is in [../spec/execution.md](../spec/execution.md); this page explains how
fast interpreters and collectors are built, and measures the choices that matter most.

## 1. From a tree to bytecode

The parser gives us a tree. The simplest way to run it is to walk it: `eval(Binary +)` calls
`eval(left)` and `eval(right)` and adds. That is easy, and slow: every node is a function call,
a `std::visit`, and a few pointer chases through memory, and the work of deciding *what* to do
is repeated every time the code runs.

A **bytecode VM** does that deciding once. A **compiler** walks the tree one time and writes a
flat array of small **instructions**; the **VM** then runs the instructions in a tight loop. For
`local c = a + b * 2`, with `a` in register 0 and `b` in register 1, Lua 5.4 writes in essence
(as `luac -l` shows it, leaving out the `MMBIN` instructions Lua adds after each arithmetic
instruction for metamethods):

```
MULK     3 1 0     ; R3 := R1 * K0      (K0 is the constant 2)
ADD      2 0 3     ; R2 := R0 + R3
```

Each instruction is one 32-bit word. Running it is: read the word, pick the handler for its
opcode, run the handler. Lua, CPython, V8's Ignition and most fast interpreters work like this.

## 2. Registers, not a stack

There are two kinds of bytecode machine:

- A **stack machine** pushes operands and pops results: `PUSH a; PUSH b; PUSH 2; MUL; ADD;
  STORE c`. Instructions are simple, but there are many of them, and each moves values around.
- A **register machine** names its operands: `MUL t, b, 2; ADD c, a, t`. Fewer instructions,
  each doing more, and values stay where they are.

Lua switched from a stack machine to a register machine in Lua 5.0, and the paper about it
measured large speedups. Lua's "registers" are slots in the current function's part of the
Lua stack: local variables live in registers, and temporaries get the next free ones. We do
the same.

## 3. Lua 5.4's instructions

One 32-bit word, in a few layouts (from `lopcodes.h`):

```
iABC   C(8) | B(8) | k(1) | A(8) | Op(7)      most instructions
iABx   Bx(17)            | A(8) | Op(7)      a constant index, a jump back
iAsBx  sBx(17, signed)   | A(8) | Op(7)      a small integer or float literal
iAx    Ax(25)                   | Op(7)      an extra argument
isJ    sJ(25, signed)           | Op(7)      a jump
```

There are 83 opcodes. Many are **specialized versions** of a general one, because each saves a
load or a check in a common case:

- `ADD R1 R2` has `ADDI R1 5` (an immediate small integer) and `ADDK R1 K3` (a constant).
- `EQ` has `EQK` and `EQI`; `LT` and `LE` have `LTI`, `LEI`, `GTI`, `GEI`.
- `GETTABLE` has `GETI` (an integer key) and `GETFIELD` (a constant string key), so `t.x` and
  `t[1]` skip a register load.
- `RETURN` has `RETURN0` and `RETURN1`, since most functions return zero or one value.
- `FORPREP`/`FORLOOP` run a numeric `for` loop with the iteration count computed once.

This is a big part of Lua's speed, and a compiler is where it happens: choosing the special
instruction, folding `2 * 3` into `6` at compile time, and reusing registers.

## 4. Dispatch: getting from one instruction to the next

Every instruction ends with "find the next handler and go there". In a loop that runs a few
billion instructions a second, how that jump is made matters.

- **`switch` in a loop.** `for (;;) { auto i = *pc++; switch (op(i)) { case ADD: ... } }`.
  Portable. The compiler makes one jump table, so every instruction goes through the same
  indirect jump, which the CPU's branch predictor finds hard to predict.
- **Computed goto** (a GCC/Clang extension). Each handler ends with its own
  `goto *labels[op(*pc++)]`. Each handler has its own jump, so the predictor learns patterns
  like "after `MUL` comes `ADD`". CPython and Lua (optionally) use this.
- **Tail calls.** Each handler is a separate function, and ends with
  `[[clang::musttail]] return handlers[op(*pc)](pc + 1, ...)`. `musttail` guarantees the call
  is a jump, so the C++ stack never grows. Each handler is a small function, and the compiler
  allocates registers for each one separately, instead of for one giant function where
  everything competes. Clang's `preserve_none` calling convention lets the VM's state (program
  counter, registers base) stay in machine registers across the jumps. CPython 3.14 added this
  style and measured a few percent to over 10%.

### Measured

A toy register VM with Lua-like instructions (tag checks on every arithmetic, an integer fast
path and a float slow path) runs a `for` loop of 7 instructions per iteration, 50 million
iterations, `clang++ -O2`, libc++, best of 9 runs. Two versions of the loop: one with `t % 7`
(an integer division, which costs as much as the dispatch around it) and one with `t & 7`
(dispatch dominates).

| Dispatch                       | Registers | with `%` | with `&` |
| ------------------------------ | --------- | -------: | -------: |
| `switch`                       | 16-byte   | 5.46 ns  | 4.96 ns  |
| `switch`                       | split     | 5.19 ns  | 4.46 ns  |
| computed goto                  | 16-byte   | 5.75 ns  | 4.94 ns  |
| computed goto                  | split     | 4.90 ns  | 4.32 ns  |
| tail calls                     | 16-byte   | 6.34 ns  | 5.42 ns  |
| tail calls                     | split     | 6.28 ns  | 5.39 ns  |
| tail calls + `preserve_none`   | 16-byte   | 5.63 ns  | 4.56 ns  |
| tail calls + `preserve_none`   | split     | 5.14 ns  | **4.09 ns** |

(Time per loop iteration, so per 7 instructions: about 0.6 ns per instruction.)

What it says:

- **Split registers** (payloads and tags in two arrays, the layout chosen in stage 4) beat
  16-byte registers in every dispatch style, by 5 to 15%.
- **Tail calls need `preserve_none`.** Without it, the normal calling convention saves and
  restores registers in every handler, and tail calls are the slowest. With it, they are the
  fastest: 4.09 ns against 4.32 for computed goto and 4.46 for `switch`, when dispatch
  dominates.
- The differences are 5 to 15% in a 12-opcode toy. Real VMs report bigger gaps with 80 opcodes
  and big handlers, where one giant function suffers most from register pressure. The real VM
  should be measured again.
- On Windows, a 16-byte struct argument is passed by reference, so a tail call cannot carry
  one (the first version of the benchmark crashed). Handlers pass the two register pointers as
  separate arguments.

The lesson for the design: write each instruction's body **once**, and keep the dispatch
around it replaceable, so it can be measured in the real VM and switched by a build option.

## 5. Calls without C++ recursion

When a Lua function calls a Lua function, the simple VM calls `execute()` recursively. Lua does
not: it pushes a small `CallInfo` record (which function, its base in the stack, where to
return) and keeps running in the same loop. Returning pops it. The C++ stack does not grow with
Lua recursion, so:

- deep Lua recursion is limited by the Lua stack (about a million slots), not by the C++ stack;
- coroutines (stage 11) can switch between Lua stacks without unwinding C++ frames;
- a runtime error unwinds by popping `CallInfo`s, not C++ frames.

**Arguments are not copied.** The caller puts the function and its arguments in consecutive
registers; the callee's registers start right after the function slot, so the arguments are
already the callee's first locals.

**Varargs** (`...`): Lua 5.4 leaves the extra arguments where the caller put them and copies the
function and its fixed parameters above them (`VARARGPREP`), so `...` stays in place below the
frame.

**The stack grows** when a function needs more registers than are left. Since it moves in
memory, nothing may keep a raw pointer into it across anything that can grow it; Lua refreshes
its `base` pointer after calls.

## 6. Upvalues

A closure can use locals of the functions around it. While the outer function runs, the local
lives in its register; when the outer function returns, the register is gone, but the closure
still needs the value. Lua's solution:

- An **open upvalue** points at the register. All closures that capture the same local share
  one upvalue object, found through a list of open upvalues sorted by stack position.
- When the local's scope ends (`return`, or a `CLOSE` at the end of a loop body), the upvalue is
  **closed**: the value is copied into the upvalue object itself, and its pointer redirected to
  that copy.

Reading an upvalue is one pointer dereference either way. A loop body's locals are fresh each
iteration, so closures created in a loop each get their own variable.

## 7. Garbage collection

Lua programs never free memory; the collector frees what can no longer be reached.

**Reachability.** The **roots** are what the program can reach directly: the stack (every
register of every running function), the global table, the registry, open upvalues. Anything
reachable from a root by following references (a closure's upvalues and prototype, a table's
keys and values, a prototype's constants) is alive; everything else is garbage.

**Mark and sweep.** Mark: start from the roots and mark every object reached. Sweep: walk every
object, free the unmarked ones, unmark the rest. Simple and correct; the program stops while it
runs, for as long as there are objects.

**Tri-color and incremental.** To avoid long pauses, the work is cut into small steps between
which the program runs. Objects are white (not seen), gray (seen, children not yet scanned) or
black (seen, children scanned). A step scans a few gray objects. The danger: the program stores
a white object into a black one that will not be scanned again, and the white one gets freed
while in use. A **write barrier** prevents it: a small check on stores into objects (table
writes, upvalue writes) that grays one of the two. Lua 5.4's default collector is incremental.

**Generational.** Most objects die young. A generational collector usually looks only at
young objects (a minor collection), and at everything rarely (a major one). Objects that
survive two minor collections become old. Lua 5.4 has a generational mode, non-moving: ages are
bits in the object header, and an old object pointing at a young one is tracked by a barrier.
For allocation-heavy programs it is much faster than incremental.

**Pacing.** The collector runs in proportion to allocation: the more the program allocates, the
more collection work it does (Lua's `GCdebt`, `pause` and `stepmul`). Memory stays bounded
without collecting more than needed.

**Non-moving.** Copying collectors (which move live objects together) allocate fastest, but a
moved object's address changes. Lua's C API gives out pointers into objects (`lua_tostring`
returns a `const char*` into the string) that must stay valid, so Lua's collector never moves
objects. Ours must not either.

### Getting memory for objects

Every object the collector manages needs memory, and every collection frees many objects at
once. Two ways:

- **Like Lua:** every object from the general allocator (`malloc` through `lua_Alloc`), all
  objects on one linked list (`allgc`), the sweep walks the list and calls `free` for the dead.
- **Size-class pages:** small objects of each size class (32, 48, 64, ... bytes) come from their
  own 64 KB pages, with a free list per class. Allocation pops a free slot; the sweep walks each
  page slot by slot in memory order and pushes dead slots back. Large objects (big strings, the
  array part of big tables) still come from `malloc`.

Measured (`clang++ -O2`, Windows' C runtime, 20 million allocations of 32 to 128 bytes, a
collection whenever the live count doubles, keeping a random half, best of 3):

| Live objects | Like Lua (`malloc` + list) | Size-class pages | Faster |
| -----------: | -------------------------: | ---------------: | -----: |
|       10,000 | 53.3 ns per object         | 28.0 ns          | 1.9×   |
|    1,000,000 | 153.1 ns per object        | 47.4 ns          | 3.2×   |

The gap grows with the heap: walking a linked list of a million objects scattered through
memory misses the cache at every step, while a page sweep reads memory in order. (Linux's
`malloc` is faster than Windows', so the gap there would be smaller; the sweep advantage stays.)

## 8. Where an aggressively fast design ends up

Putting the measurements together, a fast VM and collector for lua2:

- a **register VM** with **Lua 5.4's instruction format** and its specialized instructions, fed
  by a compiler that folds constants and picks the specialized forms;
- **tail-call dispatch with `preserve_none`**, with every instruction body written once so the
  dispatch can be switched and measured again;
- **split registers**, and fast paths (two integers, two floats) inline in the handlers, with
  the slow paths (strings, errors, later metamethods) out of line;
- **no C++ recursion** for Lua calls, and arguments passed in place;
- a **non-moving** collector with **size-class pages**, **incremental** first and
  **generational** next, with write barriers in the VM from the start;
- **measurement against the real Lua** on the same programs, all along, so "fast" means
  something.
