-- A binary chunk whose functions nest far deeper than the compiler ever
-- makes them is refused instead of exhausting the native stack (issue #4),
-- and a constant count bigger than the chunk is a format error, not an
-- allocation (issue #12).
local header = string.dump(load("")):sub(1, 31) .. "\0"
-- one level: no source, lines 0/0, 0 params, not vararg, stack 2,
-- no code, no constants, no upvalues, one child function
local level = "\x80\x80\x80\x00\x00\x02\x80\x80\x80\x81"
print(load(header .. level:rep(300000), "=nested", "b"))
-- 200 levels load (the deepest the compiler could ever make)
local leaf = "\x80\x80\x80\x00\x00\x02\x80\x80\x80\x80\x80\x80\x80\x80"
print(type(load(header .. level:rep(199) .. leaf .. ("\x80\x80\x80\x80"):rep(199), "=ok", "b")))
-- a huge constant count
local consts = "\x80\x80\x80\x00\x00\x02\x80\x07\x7f\x7f\x7f\xff"
print(load(header .. consts, "=consts", "b"))
