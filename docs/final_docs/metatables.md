# Metatables

How lua2's metatables and metamethods work (roadmap stage 7). This page describes the code as it
is. The rules are in [../spec/metatables.md](../spec/metatables.md), how the stage was planned in
[../plan/metatables.md](../plan/metatables.md), and the background in
[../learn/tables_metatables_errors.md](../learn/tables_metatables_errors.md).

**Status:** stage 7 is done (version 0.0.7, built together with stages 6 and 8 at 0.0.8): every
metamethod of Lua 5.4 except those of userdata (no userdata yet), to-be-closed variables,
finalizers and weak tables.

## Where it lives

| File                                                       | Contents                                  |
| ---------------------------------------------------------- | ----------------------------------------- |
| `lib/internal/vm/metamethods.hpp`, `lib/vm/metamethods.cpp` | `Event`, the lookups (`ltm.c`)           |
| `lib/vm/execute.cpp`, `lib/vm/handlers.inc`                | metamethod calls from the VM              |
| `lib/gc/collector.cpp`                                     | weak tables and finalizers (`lgc.c`)      |
| `lib/gc/heap.cpp`                                          | `set_metatable`, `close`                  |
| `lib/std/base.cpp`, `lib/std/auxlib.cpp`                   | `setmetatable`, `getmetatable`, `tostring`, `collectgarbage` |
| `tests/meta_test.cpp`, `tests/fixtures/my_lua/metatables/`, `close/`, `gc/` | tests              |

## Looking metamethods up

`Event` lists the events in Lua's `TMS` order, so `MMBIN`'s operand is an `Event`. The heap makes
their names (`"__index"` ...) once and keeps them forever (`luaT_init`, `Heap::event_string`).

- `metatable_of(v)`: a table's own metatable, or the one for the value's type (`Heap`'s
  `type_metatables_`, Lua's `G(L)->mt`, empty until the string library).
- `metamethod_in(mt, e)` (`fasttm`): for the events up to `__eq`, a metatable remembers in a bit
  that it lacks the event, so asking again costs one test; the bits are cleared whenever a key is
  stored into that table. The VM inlines the same lookup (`fast_metamethod`).
- `metamethod_of(v, e)` (`luaT_gettmbyobj`), `metafield(v, name)` (`luaL_getmetafield`), and
  `object_type_name(v)` (`luaT_objtypename`: `__name` in messages, M3.7).

## Calling them from the VM

A metamethod is a call through C++: `call_metamethod` pushes the function and its two operands
at the top and calls `call()` for one result (`luaT_callTMres`). Before that, the instruction
*protects* its frame (Lua's `Protect`): it saves its pc (for error positions and tracebacks) and
sets the top above all its registers, so the call cannot overwrite them and the collector sees
them. After the call the stack may have moved, so the registers are found again from the base
(`VM_RELOAD`).

- **Indexing** (`finish_get`, `finish_set`, M2): Lua's loops over `__index` and `__newindex`,
  up to 2000 steps (`MAXTAGLOOP`). Functions are called; other values are indexed in turn.
- **Arithmetic** (`arith_slow`, M3.1): the first operand's metamethod, then the second's.
  Strings have no metatable before the string library, so the slow path stands in for the
  string library's arithmetic metamethods (`lstrlib.c`'s `arith` and `trymt`): both operands
  converted, or the other operand's metamethod, or the string library's message. Unary
  operators pass the operand twice.
- **Concatenation** (`concat_slow`, M3.3): `luaV_concat` itself, pairwise from the right, joining
  every run of strings and numbers at once; the fast path joins a whole chain of strings and
  numbers in one go.
- **Comparisons** (`equal_slow`, `order_slow`, `order_metamethod`, M3.4, M3.5): `__eq` only for
  two different tables; `__lt` and `__le` for anything but numbers and strings, with
  `LUA_COMPAT_5_3`'s `__le` fallback (`not (b < a)`), since the reference Lua is built that way.
- **Length** (`length_slow`, M3.6) and **calls** (`resolve_callable`, M4.1: `__call` shifts the
  arguments up and puts the metamethod in the function's slot, as many times as needed).

## To-be-closed variables (M6)

`State::to_be_closed` lists the stack slots of the variables to close, lowest first (Lua keeps
the same list as deltas inside the stack). `TBC` adds a slot after checking the value has
`__close` (`new_to_be_closed`; `nil` and `false` are skipped); `TFORPREP` does the same for a
generic `for`'s fourth value. `close_level` (`luaF_close`) closes the upvalues at a level and
then calls `__close(value, error)` for each variable there, the last first. `CLOSE` and a
`RETURN` with its `k` bit call it with no error; a protected call that catches an error calls it
with the error object, each call placed right above its variable, since nothing above is alive
(`prepcallclosemth`), and an error in a `__close` becomes the new error while the closing goes on
(`luaD_closeprotected`).

## Finalizers (M7.1, M7.2)

A port of `lgc.c`'s finalizer machinery:

- `Heap::set_metatable` (`lua_setmetatable`) calls `check_finalizer` (`luaC_checkfinalizer`): if
  the metatable has `__gc` now, the table moves from the list of all objects to `finalizable_`
  (`finobj`) and gets the `finalized` mark bit. Generational mode keeps survival and old pointers
  into that list as for the main one.
- In the atomic phase, after weak values are cleared, `separate_to_be_finalized` moves the
  unreachable ones, in order, to `to_be_finalized_` (`tobefnz`), and `mark_being_finalized`
  marks them again: they and what they reach survive this cycle.
- `call_finalizer` (`GCTM`) takes the first one back to the list of all objects and calls its
  `__gc` in protected mode, with collection stopped (`in_finalizer_`); errors are ignored (Lua
  makes them warnings, which are off by default). Incremental mode runs ten per step in its
  `CallFinalizers` phase (`GCFINMAX`), generational mode all of them after each collection, a
  full collection all of them.
- `Heap::close` (`lua_close`, also run by the destructor) closes the main thread's pending
  to-be-closed variables, then finalizes every table still marked, reachable or not. The
  interpreter's output goes on until then, so finalizers print after a program's results, as in
  Lua.

The finalizer's frame is named `metamethod '__gc'` in tracebacks: the frame that was running when
the collector called it is flagged (`CallInfo::finalizer`, Lua's `CIST_FIN`).

## Weak tables (M7.3 to M7.5)

`traverse_table` reads `__mode` (cached like the other events): strong tables are traversed
whole; weak-value tables mark only keys (`traverse_weak_value`); weak-key tables are ephemerons
(`traverse_ephemeron`: a value is marked only once its key is); all-weak tables mark nothing.
The tables that may need clearing go to the gray lists `weak_`, `ephemeron_` and `all_weak_`, and
the atomic phase follows `lgc.c`'s order exactly: converge the ephemerons (traverse them again,
alternating direction, until nothing changes), clear weak values, separate and resurrect the
objects to finalize, converge again, clear weak keys, and clear the weak values of tables that
were resurrected. Strings are never removed from weak tables: `is_cleared` marks them instead.

## The library

`setmetatable` checks its arguments and `__metatable` (M1.3), `getmetatable` returns
`__metatable` when there is one, `tostring` and `print` use `to_display_string` (`luaL_tolstring`:
`__tostring`, which must give a string or a number, or `__name`), `pairs` calls `__pairs`, and
`collectgarbage` drives the collector (M7.6), refusing inside a finalizer as Lua's `lua_gc` does.

## Speed

`metamethods` (vector arithmetic, `__eq`, an `__index` function) runs in 0.80x of the real Lua's
time and `methods` (classes through `__index` chains) in 0.98x.
