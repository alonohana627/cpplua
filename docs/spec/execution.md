# Running Lua 5.4: what execution must do

The rules the compiler and VM implement: how chunks, statements, expressions and calls run.
It is based on the Lua 5.4 Reference Manual §2.4-§2.5 (the parts about running code),
§3.3-§3.5 (statements, expressions, visibility) and §4.4 (errors), restated in our own words.
Where the manual is silent, the reference implementation (`lvm.c`, `ldo.c`, `lcode.c`,
`ldebug.c` in Lua 5.4) is the tie-breaker, and those rules are marked **(impl)**. Every
**(impl)** rule is checked against the Lua source before its part is built.

Rules about single values and operators (types, arithmetic, comparisons, `..`, `#`) are in
[values.md](values.md); the syntax is in [grammar.md](grammar.md).

Every rule has an id like `E5.3`. Tests should name the rule they check, for example
`TEST_CASE("E5.3 an integer loop never overflows", "[vm]")`.

---

## E1. Chunks

- **E1.1** A chunk runs as the body of a function with no parameters that takes `...`
  (G2.4): the values passed to the chunk are its varargs, and its `return` values are the
  chunk's results.
- **E1.2** The chunk has one upvalue, `_ENV`, holding the environment: a global name `x` means
  `_ENV.x` (G5.6). Normally `_ENV` is the global table; whoever loads the chunk decides.
- **E1.3** Each time a chunk is loaded, it becomes a new function; running it again runs the
  same code with fresh locals.

## E2. Values in variables

- **E2.1** Variables hold values; values of type table, function, thread and userdata are
  references: assigning one copies the reference, not the object.
- **E2.2** A local variable exists from the statement after its declaration to the end of its
  block (G3.13). Each execution of a `local` statement creates a new variable.
- **E2.3** A global variable is a field of `_ENV`. Reading a global no one assigned gives
  `nil`.

## E3. Adjusting lists of values

- **E3.1** When a list of values is assigned to a list of variables, or passed as arguments,
  the list is adjusted to the needed length: extra values are dropped, missing ones are `nil`.
- **E3.2** A function call or `...` at the **end** of an expression list gives all its values;
  anywhere else it gives exactly one (its first, or `nil` if it has none).
- **E3.3** Parentheses cut to one value: `(f())` is exactly one value, and so is `(...)`.
- **E3.4** In a multiple assignment, all expressions are evaluated before any assignment is
  made: `a, b = b, a` swaps. The order of the assignments themselves is undefined by the
  manual. **(impl: they happen from right to left, so `a, a = 1, 2` leaves `a` as 1.)**

## E4. Evaluation order

- **E4.1** The operands of a binary operator are evaluated before the operation; Lua evaluates
  them left to right. **(impl)**
- **E4.2** `and` and `or` short-circuit: `a and b` is `a` if `a` is falsy, else `b`; `a or b` is
  `a` if `a` is truthy, else `b`. The second operand is evaluated only when needed. `not a` is
  `true` when `a` is falsy (V1.4).
- **E4.3** Arguments are evaluated left to right, after the function expression. **(impl)**
- **E4.4** Constant expressions may be computed at compile time (constant folding), with the
  same results the VM would give; an operation that would raise an error is not folded and
  raises its error when it runs (`2 // 0` is a runtime error). **(impl: `lcode.c`'s
  `validop` and `constfolding`)**

## E5. Control structures

- **E5.1** `if`, `while` and `repeat` test their conditions for truthiness (V1.4).
- **E5.2** The condition of `repeat ... until cond` can see the locals of the body (G3.14).
- **E5.3** A numeric `for` evaluates its three expressions once, before the loop. If the
  initial value and the step are integers, and the limit is or can be clipped to an integer,
  the loop is an integer loop; otherwise all three are converted to floats and it is a float
  loop. **(impl: `forprep` in `lvm.c`)**
- **E5.4** An integer loop runs a precomputed number of times, so it never overflows: `for i =
  math.maxinteger - 1, math.maxinteger do` runs twice and stops. A float limit is clipped to the
  integer range with floor (positive step) or ceil (negative step). **(impl: `forlimit`)**
- **E5.5** A step of zero is an error: `'for' step is zero`. A value that is not a number is
  an error: `bad 'for' initial value (number expected, got string)`, and the same with `limit`
  and `step`. **(impl: `luaG_forerror` in Lua 5.4.9)** Numeric strings are converted, as in
  arithmetic: `for i = "1", 3` runs three times, with float values, since a string is not an
  integer. **(impl: `forprep` uses `luaV_tointeger` and `tonumber`, which convert strings;
  checked against the real Lua 5.4.9)**
- **E5.6** The loop variable is a fresh local in each iteration: changing it does not change
  the iteration, and closures capture each iteration's own variable.
- **E5.7** A generic `for` evaluates its expression list once, adjusted to four values (E3.1):
  the iterator function, the state, the initial control value, and a closing value. Each
  iteration calls `f(state, control)`; if the first result is `nil` the loop ends, otherwise
  the results are assigned to the loop variables and the first becomes the next control value.
  The closing value is a to-be-closed variable ([metatables.md](metatables.md), M6.4).
- **E5.8** `break` leaves the innermost loop. `goto` jumps to a visible label (G8).

## E6. Functions and calls

- **E6.1** Evaluating a function expression creates a new closure each time, with its current
  upvalues (E7).
- **E6.2** A call evaluates the function value and the arguments (E4.3), adjusts the arguments
  to the parameters (E3.1), and runs the body with them as fresh locals. Extra arguments go to
  `...` in a vararg function, and are dropped otherwise.
- **E6.3** Calling a value that is not a function is an error: `attempt to call a X value`, with
  variable information (E8.2), unless it has a `__call` metamethod ([metatables.md](metatables.md),
  M4.1).
- **E6.4** `return` ends the function with the values of its expression list; a function that
  ends without `return` returns no values.
- **E6.5** `return f(args)` is a **tail call**: the caller's frame is reused, so there is no
  limit on the depth of tail calls (a tail-recursive loop runs forever in constant space).
  Parentheses make it a normal call: `return (f(args))` returns exactly one value and is not a
  tail call.
- **E6.6** `...` in a vararg function gives the extra arguments (E3.2). `select('#', ...)` and
  friends come with the standard library.
- **E6.7** Deep recursion that exhausts the Lua stack is the error `stack overflow`, at the
  call that would pass the limit (`test.lua:3: stack overflow`); the stack holds about a million
  values. **(impl: `LUAI_MAXSTACK` = 1,000,000; `luaD_growstack` raises the bare message, and
  the position is the calling instruction's)**
- **E6.8** A method call `obj:m(args)` evaluates `obj` once and passes it as the first argument
  (G4.9).

## E7. Upvalues

- **E7.1** A closure refers to the local variables of the enclosing functions it uses, not to
  copies of their values: if the variable changes, every closure sees the change.
- **E7.2** Closures that capture the same variable share it.
- **E7.3** A captured variable lives as long as some closure uses it, even after its function
  returned.
- **E7.4** Each iteration of a loop has new locals (E5.6), so closures made in different
  iterations capture different variables.

## E8. Runtime errors

- **E8.1** A runtime error stops the program unless caught (`pcall`, stage 8). Its message
  starts with the position of the instruction that raised it: `chunkname:line: message`, as in
  `test.lua:3: attempt to perform arithmetic on a nil value (local 'x')`.
- **E8.2** When the faulty value came from a named place, the message says which, in
  parentheses after the value: `(local 'x')`, `(global 'x')`, `(upvalue 'x')`, `(field 'x')`,
  `(method 'm')`, `(constant 'x')`. **(impl: `varinfo` and `getobjname` in `ldebug.c`, which
  work backwards from the instruction to find where the value came from)**
- **E8.3** Errors from operators use the messages of [values.md](values.md) (V3.12, V4.5, V8.6,
  V9.2, V9.4), with E8.1 and E8.2 added.
- **E8.4** The main chunk's `_ENV` is the globals table, an ordinary table
  ([tables.md](tables.md)) holding the standard library so far. Catching errors, error objects
  and tracebacks are in [errors.md](errors.md).

## E9. Garbage collection

- **E9.1** Memory of objects that can no longer be reached from the program is reclaimed
  automatically; a program never frees anything itself.
- **E9.2** Collection is invisible to a correct program: no reachable object is ever freed or
  changed, and no value changes when objects are collected. Only memory use and timing change.
- **E9.3** The collector does not move objects: the address of an object, and of a string's
  bytes, stays the same as long as the object is alive. **(impl: the C API relies on this)**
- **E9.4** Collection runs on its own, in proportion to allocation, so memory stays bounded for
  programs whose live data is bounded.
- **E9.5** There are **two collection modes** (manual §2.5), and a program can switch between
  them at any time:
  - **incremental** (the default): a full mark-and-sweep cycle done in small steps interleaved
    with the program. Tuned by the *pause* (how much memory may grow before a new cycle
    starts, default 200%), the *step multiplier* (how much work each step does relative to
    allocation, default 100) and the *step size* (default 2^13 bytes);
  - **generational**: frequent *minor* collections that only look at young objects, and
    occasional *major* ones that look at everything. Tuned by the *minor multiplier* (default
    20%) and the *major multiplier* (default 100%).
  **(impl: the defaults are `LUAI_GCPAUSE`, `LUAI_GCMUL`, `LUAI_GCSTEPSIZE`, `LUAI_GENMINORMUL`
  and `LUAI_GENMAJORMUL`, checked against the source when built)**
- **E9.6** A program controls the collector through `collectgarbage` (standard library, stage
  9): `"collect"` (a full cycle), `"step"`, `"stop"`, `"restart"`, `"isrunning"`, `"count"`
  (memory in use, in KB), `"incremental"` and `"generational"` (switch mode and set its
  parameters, returning the previous mode). Until the library exists, the same controls are a
  C++ interface of the collector.
