# Binary chunks

**Status:** stage 11 (version 0.0.11): `string.dump` makes Lua 5.4 binary chunks and `load`
loads them, the real Lua's and `luac`'s included. The rules are P2 in
[../spec/conformance.md](../spec/conformance.md).

## What exists

| Function | Port of |
| -------- | ------- |
| `dump(proto, strip)` (`lib/internal/vm/dump.hpp`) | `ldump.c`'s `luaU_dump` |
| `undump(heap, bytes, name)` | `lundump.c`'s `luaU_undump` |
| `string.dump`, and `load`, `loadfile`, `dofile`, `lua2 file` taking binary chunks | `lstrlib.c`'s `str_dump`, `ldo.c`'s `f_parser` |

lua2's output is byte for byte the real Lua's: `string.dump` of the same function in `lua2` and
in `lua_ref` gives the same string, stripped or not, since the compiler emits Lua's bytecode
(stage 5) and the dump writes Lua's format.

## Where it lives

| File | Contents |
| ---- | -------- |
| `lib/vm/dump.cpp` | `Dumper` (header, functions, constants, upvalues, debug information) and the loader |
| `lib/vm/run.cpp` | `load_chunk`: a chunk starting with `"\x1b"` is binary (when the mode allows it, P2.2); its first upvalue becomes the globals |
| `lib/std/string.cpp` | `str_dump` |
| `lib/internal/vm/proto.hpp` | `UpvalueInfo::kind` (the variable's kind, `VDKREG` and the others), saved in chunks as Lua saves it |

## How it works

**The format** is `lundump.h`'s: the header (`"\x1bLua"`, version `0x54`, format 0, the check
bytes `"\x19\x93\r\n\x1a\n"`, the sizes of an instruction, an integer and a float, and a check
integer and float), the main function's number of upvalues, then the function: source, lines,
parameters, code, constants, upvalue descriptions, nested functions and the debug information.
Sizes are written 7 bits a byte, most significant first, with the last byte's high bit set.

**Line information is converted.** lua2's prototypes keep each instruction's absolute line. Lua
keeps a signed byte per instruction (the difference from the line before) and, every 128
instructions or when the difference does not fit, an entry in a table of absolute lines,
marked `-0x80` in the byte array (`lcode.c`'s `savelineinfo`). Dumping computes Lua's encoding
from the lines, the same way the real compiler would have; loading turns it back into a line
per instruction.

**Loading checks what Lua checks**: the header (each mismatch has its message: `version
mismatch`, `format mismatch`, `corrupted chunk`, `Instruction size mismatch`, `integer format
mismatch`, ...) and that nothing is cut short (`truncated chunk`), each prefixed with
`name: bad binary format (`. The code itself is not verified, as in Lua (P2.3). A chunk without
a source (stripped) is named `=?`.

A file whose first line starts with `#` (a `#!` line) and goes on with a binary chunk loads
too: the newline normally kept after that line, for line numbers, is dropped before a binary
chunk.

## Tests

- `tests/stdlib_test.cpp`: "S2.5 string.dump makes Lua 5.4 binary chunks" (round trips,
  `strip`, upvalues), and `load`'s errors on truncated chunks.
- The suite's `calls.lua`, `db.lua`, `errors.lua` and `files.lua` (dumps and loads, with and
  without `strip`, binary files with a `#` line), and `all.lua`, which runs every test file
  through `string.dump` and `load`.
