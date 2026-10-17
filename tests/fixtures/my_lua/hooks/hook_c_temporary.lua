-- debug.getlocal in a call hook sees the hooked C function's stack slots as
-- "(C temporary)", as with ldblib's hookf (issue #7).
local out = {}
debug.sethook(function(e)
  local info = debug.getinfo(2, "S")
  if info.what == "C" then out[#out+1] = tostring(debug.getlocal(2, 1)) end
end, "c")
math.abs(-3)
debug.sethook()
print(table.concat(out, " "))
