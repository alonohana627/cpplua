-- A count that does not fit an int is cut to one first, as Lua's
-- (int)luaL_optinteger does: 2^32 is 0, so with an empty mask no hook is
-- set (issue #7).
debug.sethook(print, "", 2^32)
print(debug.gethook())
debug.sethook(print, "", 2^32 + 5)
print(select(2, debug.gethook()), select(3, debug.gethook()))
debug.sethook()
