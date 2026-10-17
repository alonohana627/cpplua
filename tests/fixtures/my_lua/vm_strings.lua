-- Strings through the VM (stage 5): concatenation of whole chains in one
-- CONCAT, numbers written the way Lua writes them, lengths, long strings,
-- and string constants shared between functions.
local function repeat_string(s, n)
  local out = ""
  for _ = 1, n do out = out .. s end
  return out
end

local i, f = 42, 0.5
local long = repeat_string("0123456789", 10)      -- 100 bytes: a long string
local short = repeat_string("ab", 20)             -- 40 bytes: still short
local zeros = "a\0b\0c"

return "x" .. i .. "y" .. f .. "z", 1 .. 2, 1.0 .. "", -0.0 .. "", 2^63 .. "",
  -9223372036854775807 - 1 .. "", 1e100 .. "", 0.1 .. "", 1/3 .. "", (1/0) .. "", -(1/0) .. "",
  #long, #short, #zeros, #"", #(long .. short), long == repeat_string("0123456789", 10),
  short == "abababababababababababababababababababab", zeros .. "!",
  "multi\
line", [[long
bracket]], [==[with ]] inside]==], "\65\066\x43\u{44}\z
       E", "\u{7FF}\u{FFFF}" .. "", #"\u{10FFFF}"
