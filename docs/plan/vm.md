# VM and garbage collector plan

Roadmap stage 5, cut into parts. The goal is a VM and collector that are **aggressively
efficient**: every choice below that affects speed was measured, or will be measured in the
part that makes it, against the real Lua 5.4.9 running the same programs. Each part ends with
everything green (`build`, `ctest`, `tidy`, `format-check`, and the sanitizer and GC-stress
runs from part 1 on). Rule ids such as `E5.3` point to [../spec/execution.md](../spec/execution.md),
`V` ids to [../spec/values.md](../spec/values.md).

Background reading, with the measurements, is in [../learn/vm_gc.md](../learn/vm_gc.md). How
the finished code works will be recorded in [../final_docs/vm.md](../final_docs/vm.md).

Finishing the last part completes the stage and bumps the version to 0.0.5.

## Decisions to make in part 0

- **Scope of the stage.** Calls need closures, and the collector must trace closures and
  upvalues, so building them separately later would mean redoing the call path and the
  collector. **Recommended: closures and upvalues move into this stage.** Roadmap stage 7
  ("Closures + upvalues") then disappears: compile-time `<const>` constants join this stage's
  compiler, and `<close>` joins metatables (stage 8), since closing calls `__close`. Tables stay
  in stage 6; until then `_ENV` is `nil` and global access fails exactly as in Lua (E8.4). This
  changes the roadmap, so it is your call.
- **Instruction format.** **Recommended: Lua 5.4's**: 32-bit instructions (7-bit opcode, 8-bit
  `A`, `B`, `C`, the `k` bit, 17-bit `Bx`, 25-bit `sJ`) and its opcode set, including the
  specialized forms (`ADDI`, `ADDK`, `EQK`, `LTI`, `RETURN1`, ...). Dense (four instructions
  per 16 bytes of cache), and the compiler can learn from `lcode.c`, which is tuned for exactly
  this set. Our own numbering; no binary compatibility with `luac` (as the roadmap says).
- **Dispatch.** Measured in the learn page, section 4: **recommended: tail calls between
  handlers with `[[clang::musttail]]` and `preserve_none`**, the fastest (4.09 ns per loop
  iteration, against 4.32 for computed goto and 4.46 for `switch`). Every instruction body is
  written once, and the dispatch around the bodies is a build option (`LUA2_DISPATCH=tailcall|goto|switch`),
  so it is measured again in the real VM (part 11), and a compiler without `preserve_none` can
  still build lua2.
- **Registers.** **Decided in stage 4: split** tags and payloads; measured again here, faster in
  every dispatch style (5 to 15%).
- **Where the compiler starts.** Lua compiles while parsing, in one pass. We have a tree.
  **Recommended: compile from the AST**, a separate pass: the parser stays as it is, and the
  compiler can look at a whole expression before choosing instructions. It follows `lcode.c`'s
  techniques (expression descriptors, jump lists, constant folding, register reuse).
- **Calls.** **Recommended: like Lua**: `CallInfo` frames, no C++ recursion for Lua-to-Lua
  calls, arguments in place, Lua 5.4's vararg layout, tail calls that reuse the frame.
- **Errors inside the VM.** The hot path must not pay for errors. **Recommended:** handlers
  test for the error case inline and jump to one shared, out-of-line error exit; the VM returns
  a status (`std::expected`) to its C++ caller. No exceptions in the loop. `pcall` (stage 9)
  catches at a `CallInfo` boundary.
- **The collector.** **Recommended:**
  - **non-moving** (E9.3), like Lua;
  - **size-class pages** for small objects (measured 1.9 to 3.2 times faster than `malloc` plus a
    list), the system allocator for large ones;
  - both of Lua 5.4's **collection modes** (E9.5, manual §2.5): **incremental** tri-color
    marking (the default) and **generational**, both in this stage, switchable at run time,
    with write barriers in the VM from the start, so neither needs the VM changed later. Part
    1's stop-the-world collector is only the first step: it becomes the incremental mode's
    full cycle (`collectgarbage("collect")`);
  - Lua's pacing (debt, pause, step multiplier);
  - a **stress mode** that collects at every allocation, used by the tests, which is the only
    reliable way to find a missing root or barrier.
- **Measuring against Lua.** **Recommended:** build the real Lua 5.4.9 from its source as a
  reference (`lua_ref`), fetched by CMake from lua.org with a pinned SHA-256, behind an option
  (`LUA2_REFERENCE_LUA`, on by default). Benchmarks run the same programs on both; tests can
  compare outputs. This downloads the source tarball once, at configure time.

## Where the code goes

```
lib/internal/vm/opcodes.hpp      instruction format, opcodes, encode/decode helpers
lib/internal/vm/proto.hpp        Proto: code, constants, nested protos, line info
lib/internal/vm/compiler.hpp     AST to Proto
lib/internal/vm/vm.hpp           State, CallInfo, the stack, execute()
lib/internal/vm/dispatch_*.hpp   the dispatch styles around the instruction bodies
lib/internal/vm/handlers.inc     every instruction body, written once
lib/internal/gc/                 object header, pages, collector, barriers
lib/vm/, lib/gc/                 implementations
include/lua2/                    a first public entry: load and run a chunk
tests/vm_*.cpp, tests/gc_*.cpp   tests, tagged [vm] and [gc]
bench/                           benchmark programs and the runner
```

## Testing approach

- **Behavior:** Lua programs in `tests/fixtures/my_lua/` with their expected results, run by a
  test that loads each one and compares what it returns (and, when the reference Lua is built,
  compares with what `lua_ref` returns for the same program). Unit tests name their rules.
- **The compiler:** `lua2 --bytecode file.lua` lists the instructions, like `luac -l`; golden
  listings check that the compiler picks the specialized instructions it should.
- **The collector:** every VM test runs a second time in GC stress mode, and once under
  AddressSanitizer (a new `asan` CMake preset).
- **Speed:** `bench/` holds benchmark programs (loops, arithmetic, recursion, closures, string
  building); a runner times lua2 and `lua_ref` on each and prints the ratio. Each part that
  touches the hot path reports its numbers.

## Parts

### Part 0: Foundations

- Settle the decisions above.
- The reference Lua (`lua_ref`), the `asan` preset, and the benchmark runner.
- Instruction encoding and decoding (`opcodes.hpp`), with `static_assert` tests; `Proto`.
- `lua2 --bytecode` listing.
- **Rules**: none yet; this is the ground the rest stands on.

### Part 1: The collector's core

- The object header, size-class pages, large objects, GC-owned strings (the stage 4 `Heap`
  becomes the collector), the string table cleaned in the sweep.
- Stop-the-world mark and sweep, roots, pacing, and the stress mode.
- **Tests**: objects survive exactly when reachable; the string table forgets dead strings;
  memory stays bounded in a loop that allocates; ASan clean.
- **Rules**: E9.1-E9.4.

### Part 2: Compiler and VM: expressions and `return`

- The compiler's core: registers, constants, expression descriptors, constant folding.
- The VM's core: the stack, `execute()`, the dispatch, `MOVE`, `LOADI`, `LOADF`, `LOADK`,
  `LOADNIL`, `LOADTRUE`, `LOADFALSE`, arithmetic and bitwise with their `K` and `I` forms,
  `UNM`, `BNOT`, `NOT`, `RETURN0`, `RETURN1`, `RETURN`.
- Locals and assignments to them.
- **Rules**: E1.1, E1.3, E2.2, E3.4, E4.1, E4.4, and V3, V4 through the VM.

### Part 3: Comparisons and control flow

- `EQ`, `LT`, `LE` and their `K`/`I` forms, `TEST`, `TESTSET`, `JMP`; `and`, `or`, `not` with
  jump lists; `if`, `while`, `repeat`, `break`, `goto` and labels.
- **Rules**: E4.2, E5.1, E5.2, E5.8, and V7, V8 through the VM.

### Part 4: Numeric `for`

- `FORPREP` and `FORLOOP` with Lua 5.4's semantics: precomputed iteration count, float loops,
  limit clipping, the errors.
- **Rules**: E5.3-E5.6.

### Part 5: Strings in the VM

- String constants, `CONCAT` over a register range (one allocation for a whole chain), `LEN`,
  string arithmetic.
- **Rules**: V3.11, V9 through the VM.

### Part 6: Functions and calls

- `CLOSURE` (without upvalues yet), `CALL`, `TAILCALL`, `RETURN` with multiple values,
  `VARARGPREP`, `VARARG`; `CallInfo` frames, no C++ recursion; stack growth; `stack overflow`;
  the generic `for` over functions (`TFORPREP`, `TFORCALL`, `TFORLOOP`).
- **Rules**: E3.1-E3.3, E4.3, E5.7, E6.1-E6.8.

### Part 7: Upvalues

- Open and closed upvalues, shared and closed like Lua's; `GETUPVAL`, `SETUPVAL`, `CLOSE`;
  closures in loops; the collector traces closures, prototypes and upvalues; `_ENV` is `nil`,
  so globals fail like Lua (`GETTABUP`, `SETTABUP`).
- **Rules**: E1.2, E2.3, E7.1-E7.4, E8.4.

### Part 8: Runtime errors with positions and names

- Line information per instruction (compact, like Lua's `lineinfo`), `chunk:line:` on every
  runtime error, and the variable information of `getobjname`.
- **Rules**: E8.1-E8.3.

### Part 9: Incremental collection

- Lua's default mode (E9.5): tri-color marking in steps, gray lists, write barriers in the VM
  (upvalues now; tables in stage 6), steps driven by allocation debt, with Lua's parameters
  (pause, step multiplier, step size) and a C++ interface for E9.6's controls.
- **Tests**: everything again in stress mode with the smallest steps.

### Part 10: Generational collection

- Lua 5.4's second mode (E9.5): object ages, minor and major collections with Lua's
  multipliers, the barriers it needs; switching between the two modes at run time, as
  `collectgarbage("generational")` and `("incremental")` will.
- **Tests**: allocation-heavy programs keep bounded memory; stress mode in both modes;
  benchmarks against incremental mode and against `lua_ref`.

### Part 11: Speed

- Run the benchmarks; profile; measure the three dispatch styles and the register layouts in
  the real VM and settle them; tune the hot handlers and the compiler's choices.
- **Target: as fast as `lua_ref` or faster on every benchmark.** Where lua2 is slower, the reason
  is found and either fixed or written down.

### Part 12: Running files

- `lua2 file.lua` runs a chunk (with what it returns printed, until `print` exists in the
  standard library); the official test suite's files still parse; bump the version to 0.0.5.
  The stage is done.

## How we work through a part

Same as before: you name the part, it is implemented with its tests, its section of
`docs/final_docs/vm.md` is written, and nothing is committed until you approve it.
