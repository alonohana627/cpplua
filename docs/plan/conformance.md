# Conformance plan

Roadmap stage 11: run the official Lua 5.4.9 test suite and work through its failures. Rule ids
such as `P2.1` point to [../spec/conformance.md](../spec/conformance.md). What was found and
fixed is in [../final_docs/conformance.md](../final_docs/conformance.md); the background is in
[../learn/conformance.md](../learn/conformance.md).

## Status: done (0.0.11): `all.lua` prints `final OK !!!`

## Decisions

- **The suite runs whole, unchanged**, through `all.lua`, with `_port=true` because this is Windows (P1). No test file is edited or skipped by lua2;
  only what `_port` and the absence of Lua's internal test library skip for the real Lua too.
- **Missing features are added, not worked around.** The suite needs binary chunks
  (`string.dump`, `load` of `luac` output), the stand-alone program (`arg`, `-e`, `LUA_INIT`),
  and the locale. Stage 9 had answered "not supported" for binary chunks; stage 11 replaces
  that with ports of `ldump.c` and `lundump.c` (P2), in Lua's exact format.
- **Every fix is checked against the real Lua** (`lua_ref`) on the same input before it is
  kept.
- **A CTest test runs the suite** in every build, so it stays green (P1.1).

## Parts

1. Run each file of the suite alone, then `all.lua`; list the failures.
2. **Binary chunks** (`lib/vm/dump.cpp`): dump and undump, and `load` taking them.
3. **The stand-alone program** (`lib/standalone.cpp`): a port of `lua.c`; `src/main.cpp` keeps
   lua2's own debug options in front of it.
4. **The locale** (`lib/value/locale.cpp`): string order, numerals, character classes.
5. **Bugs** the suite found in earlier stages, each fixed at its cause
   ([../final_docs/conformance.md](../final_docs/conformance.md) lists them).
6. The suite as a CTest test; docs; version 0.0.11.
