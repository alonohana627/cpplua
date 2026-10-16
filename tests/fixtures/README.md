# Test fixtures

## `lua-5.4.9-tests/`

The official Lua 5.4.9 test suite, unchanged, from
<https://www.lua.org/tests/lua-5.4.9-tests.tar.gz> (137,260 bytes, downloaded 2026-10-16).

Copyright (C) 1994-2025 Lua.org, PUC-Rio, under the MIT license; the full notice is at the end
of `lua-5.4.9-tests/all.lua`.

`.gitattributes` keeps these files byte for byte, so Git never converts their line endings.

Used by `tests/conformance_test.cpp` (tag `[conformance]`). Today it lexes every `.lua` file;
later stages will parse and finally run them (roadmap stage 14).
