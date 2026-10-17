# Lua 5.4 metatables and metamethods

The rules our metatables implement (roadmap stage 7): metatables, the metamethods of indexing,
operators and calls, `tostring`, to-be-closed variables, finalizers and weak tables. It is
based on the Lua 5.4 Reference Manual §2.4 (metatables and metamethods), §2.5.3 (finalizers),
§2.5.4 (weak tables), §3.3.8 (to-be-closed variables) and §6.1 (`setmetatable`,
`getmetatable`, `tostring`, `pairs`, `collectgarbage`), restated in our own words. Where the
manual is silent, the reference implementation (`ltm.c`, `lvm.c`, `lfunc.c`, `lgc.c`,
`lauxlib.c` in Lua 5.4.9, built like Lua's makefile with `LUA_COMPAT_5_3`) is the
tie-breaker, and those rules are marked **(impl)**.

Every rule has an id like `M3.4`. Tests should name the rule they check, for example
`TEST_CASE("M3.4 __eq is only tried for two tables", "[meta]")`.

---

## M1. Metatables

- **M1.1** Every table can have a metatable, an ordinary table whose fields (*metamethods*,
  named `__add`, `__index`, ...) change how the table behaves. `setmetatable(t, mt)` sets it (or
  removes it with `nil`) and returns `t`; `getmetatable(v)` returns it, or `nil`.
- **M1.2** Values of other types have one metatable per type, which only the C API, the debug
  library and the string library (stage 9, for strings) can set; until then they have none.
- **M1.3** If the metatable has a `__metatable` field, `getmetatable` returns that field's value
  instead, and `setmetatable` on the table is the error `cannot change a protected metatable`.
- **M1.4** A metamethod is read from the metatable with a raw access when the operation runs,
  so changing the metatable later changes the behavior, except for `__gc` (M7.1) and `__mode`
  (M7.3), which are read when the collector needs them.
- **M1.5** `setmetatable` checks its arguments: `bad argument #1 to 'setmetatable' (table
  expected, got X)` and `bad argument #2 to 'setmetatable' (nil or table expected)`.

## M2. Indexing

- **M2.1** Reading `t[k]`: if `t` is a table and `t[k]` is present, that value. Otherwise the
  `__index` metamethod of `t` (for a non-table, of its type): if absent, the result is `nil` for
  a table and the error `attempt to index a X value` (T1.5) for anything else; if it is a
  function, the result is its first result when called with `(t, k)`; if it is any other value
  `h`, the result is `h[k]`, by these same rules (so `h` can have its own `__index`).
- **M2.2** Writing `t[k] = v`: if `t` is a table and `t[k]` is present, a raw assignment.
  Otherwise the `__newindex` metamethod: if absent, a raw assignment for a table (T1.4's errors)
  and the error `attempt to index a X value` for anything else; if it is a function, it is called
  with `(t, k, v)`; if it is any other value `h`, the assignment is `h[k] = v` by these rules.
- **M2.3** A chain of `__index` or `__newindex` values (not functions) longer than 2000 is the
  error `'__index' chain too long; possible loop` or `'__newindex' chain too long; possible
  loop`. **(impl: `MAXTAGLOOP`)**
- **M2.4** Globals are fields of `_ENV` (E2.3), so they follow M2.1 and M2.2 too.
- **M2.5** `rawget`, `rawset` and `rawequal` bypass metamethods (T5.1).
- **M2.6** `pairs(t)` calls `__pairs(t)` when `t` has one and returns its first three results;
  `ipairs` reads with M2.1.

## M3. Operators

- **M3.1** Arithmetic and bitwise operators (`+ - * / % ^ //`, `& | ~ << >>`) first try the
  primitive operation on numbers (and, for arithmetic, strings that convert to numbers, V5).
  If that is not possible, the first operand's metamethod (`__add`, `__sub`, `__mul`, `__div`,
  `__mod`, `__pow`, `__idiv`, `__band`, `__bor`, `__bxor`, `__shl`, `__shr`) is called with both
  operands in their original order, or else the second operand's; the result is its first
  value. With neither, the error of the primitive operation (V3.12, V4.5) with E8.2's names.
- **M3.2** Unary `-` and `~` work the same way with `__unm` and `__bnot`, called with the
  operand twice. **(impl: `luaT_trybinTM(L, rb, rb, ra, TM_UNM)`)**
- **M3.3** `a .. b`: strings and numbers are joined (V9.1); otherwise `__concat` of `a`, else of
  `b`, called with `(a, b)`. A chain `a .. b .. c` is grouped to the right: `a .. (b .. c)`, and
  every run of strings and numbers is joined at once. **(impl: `luaV_concat`)**
- **M3.4** `a == b`: values that are primitively equal are equal, and values of different types
  are never equal, without metamethods. Two different tables use `__eq` of `a`, else of `b`,
  called with `(a, b)`; the result is converted to a boolean. `a ~= b` is `not (a == b)`.
- **M3.5** `a < b` and `a <= b`: numbers and strings compare primitively (V8); anything else
  uses `__lt` or `__le` of `a`, else of `b`, called with `(a, b)`, the result converted to a
  boolean. `a > b` is `b < a` and `a >= b` is `b <= a`. Without a metamethod, the error
  `attempt to compare two X values` or `attempt to compare X with Y`. **(impl, `LUA_COMPAT_5_3`:
  with no `__le`, `a <= b` is `not (b < a)` through `__lt`)**
- **M3.6** `#v`: a table without `__len` gives its border (T3); a string its length; otherwise
  `__len` is called with `(v, v)` and its first result is the length, whatever its type.
- **M3.7** In error messages, a table whose metatable has a string `__name` is called by that
  name: `attempt to index a Point value`. **(impl: `luaT_objtypename`)**

## M4. Calls

- **M4.1** Calling a value that is not a function calls its `__call` metamethod with the value
  inserted as the first argument: `obj(a, b)` is `__call(obj, a, b)`. The metamethod may itself
  be a callable object. Without `__call`, the error of E6.3.

## M5. `tostring`

- **M5.1** `tostring(v)` and `print` use `__tostring(v)` when present; it must return a string,
  else the error `'__tostring' must return a string`. Otherwise a table with a string `__name`
  shows as `NAME: 0x...`; other values as in V6 and `display_text`.

## M6. To-be-closed variables

- **M6.1** `local x <close> = v` requires `v` to be `nil`, `false`, or a value with a `__close`
  metamethod when the declaration runs; anything else is the error `variable 'x' got a
  non-closable value`.
- **M6.2** When the variable goes out of scope (the block ends, or `break`, `goto` or `return`
  leaves it), `__close(v, nil)` is called. Several variables are closed in the reverse order of
  their declarations. `nil` and `false` are not closed.
- **M6.3** When an error leaves the scope, `__close(v, e)` is called with the error object `e`,
  before the error reaches `pcall`. An error inside `__close` then becomes the new error, and the
  other variables are still closed; an error inside `__close` on a normal exit is an ordinary
  error.
- **M6.4** The fourth value of a generic `for` is to-be-closed: it is closed when the loop ends,
  by `break` or by an error.

## M7. Garbage collection metamethods

- **M7.1** A table is *marked for finalization* when `setmetatable` gives it a metatable that
  has a `__gc` field at that moment; setting `__gc` later does not mark it.
- **M7.2** When a marked table becomes unreachable, the collector does not free it: it calls
  `__gc(t)` (if the field is still a function, at that time), and the table, with what it refers
  to, stays alive until the finalizer has run and it is unreachable again. Each table is
  finalized once. Finalizers run as part of collection (`collectgarbage()` runs all pending
  ones), in the reverse order in which the tables were marked. When the interpreter closes,
  every marked table is finalized, reachable or not. Errors in finalizers are ignored.
  **(impl: Lua turns them into warnings, which are off by default)**
- **M7.3** A metatable's `__mode` makes the table *weak*: `"k"` weak keys, `"v"` weak values,
  `"kv"` both. An entry whose weak key or weak value is collected disappears from the table.
  Strings and non-object values (numbers, booleans, C++ functions) are never removed.
- **M7.4** A weak-key table is an *ephemeron* table: its value is reachable through the entry
  only if its key is reachable some other way, so a value that refers to its own key does not
  keep the entry alive.
- **M7.5** Objects being finalized are removed from weak values before their finalizers run,
  and from weak keys only after they are collected for good.
- **M7.6** `collectgarbage(opt)`: `"collect"` (the default) runs a full cycle and returns 0;
  `"count"` returns the memory in use in KB as a float; `"step"` runs a step and returns whether
  it finished a cycle; `"stop"` and `"restart"` stop and restart automatic collection, and
  `"isrunning"` tells which; `"incremental"` and `"generational"` switch mode and return the
  previous one's name. An unknown option is `bad argument #1 to 'collectgarbage' (invalid option
  'x')`.
