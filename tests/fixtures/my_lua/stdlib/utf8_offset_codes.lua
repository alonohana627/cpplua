-- utf8.offset and utf8.codes (stage 9): offset of the n-th character from
-- the start, from a given position, backwards with negative n, n = 0 finding
-- the start of the character containing a byte, positions past the end, the
-- continuation-byte error; codes iterating positions and code points, lax
-- mode, and the errors for invalid sequences met during the iteration.

local s = "a\u{E9}\u{20AC}\u{1F600}z"   -- byte positions 1, 2, 4, 7, 11; length 11
print(utf8.offset(s, 1), utf8.offset(s, 2), utf8.offset(s, 3), utf8.offset(s, 4), utf8.offset(s, 5))
print(utf8.offset(s, 6), utf8.offset(s, 7), utf8.offset(s, -1), utf8.offset(s, -2), utf8.offset(s, -5))
print(utf8.offset(s, -6), utf8.offset(s, 2, 4), utf8.offset(s, -1, 7), utf8.offset(s, 1, 12))
print(utf8.offset(s, 0, 1), utf8.offset(s, 0, 3), utf8.offset(s, 0, 6), utf8.offset(s, 0, 9), utf8.offset(s, 0, 12))
print(utf8.offset("", 1), utf8.offset("", -1), utf8.offset("", 0), utf8.offset("abc", 4), utf8.offset("abc", 5))
print(pcall(utf8.offset, s, 1, 3))
print(pcall(utf8.offset, s, 1, 13))
print(pcall(utf8.offset, s, 1, 0))
print(pcall(utf8.offset, s))

-- Every character with codes.
local out = {}
for pos, code in utf8.codes(s) do out[#out + 1] = pos .. ":" .. code end
print(table.concat(out, " "))
local count = 0
for _ in utf8.codes("") do count = count + 1 end
print(count)

-- The iterator by hand.
local iter, state, init = utf8.codes("hi")
print(type(iter), state, init)
print(iter(state, init))
print(iter(state, 1))
print(iter(state, 2))

-- Lax mode accepts surrogates.
out = {}
for pos, code in utf8.codes("a\xED\xA0\x80b", true) do out[#out + 1] = pos .. ":" .. code end
print(table.concat(out, " "))

-- Invalid sequences raise an error during the iteration.
local function collect(str)
  local got = {}
  for _, code in utf8.codes(str) do got[#got + 1] = code end
  return table.concat(got, ",")
end
print(pcall(collect, "ab\xFFcd"))
print(pcall(collect, "ab\xED\xA0\x80"))
print(pcall(collect, "\xE2\x82"))
print(pcall(collect, "ok\x80"))
print(pcall(collect, "fine"))
print(pcall(utf8.codes))
local num_iter, num_state, num_init = utf8.codes(42)
print(type(num_iter), num_state, type(num_state), num_init)

-- Reversing a string by characters with offset.
local rev = {}
local n = utf8.len(s)
for i = n, 1, -1 do
  local first = utf8.offset(s, i)
  local after = utf8.offset(s, i + 1)
  rev[#rev + 1] = s:sub(first, after - 1)
end
local reversed = table.concat(rev)
print(#reversed, utf8.codepoint(reversed, 1, -1))

return utf8.offset(s, 3), utf8.len(s)
