-- utf8.char, utf8.charpattern, utf8.codepoint and utf8.len (stage 9): one to
-- four byte sequences, the extended range up to 0x7FFFFFFF, ranges with
-- negative indices, lax mode accepting surrogates and big code points, len
-- returning nil plus the position of the first invalid byte, and errors.
-- Bytes are shown in hex so the output stays ASCII.

local function hex(s)
  return (s:gsub(".", function(c) return string.format("%02X ", c:byte()) end))
end

print(hex(utf8.char(65)), hex(utf8.char(0xE9)), hex(utf8.char(0x20AC)), hex(utf8.char(0x1F600)))
print(hex(utf8.char(0x7F)), hex(utf8.char(0x80)), hex(utf8.char(0x7FF)), hex(utf8.char(0x800)))
print(hex(utf8.char(0xFFFF)), hex(utf8.char(0x10000)), hex(utf8.char(0x10FFFF)))
print(hex(utf8.char(0x110000)), hex(utf8.char(0x7FFFFFFF)))
print(utf8.char(72, 105, 33), utf8.char() == "", #utf8.char(0), hex(utf8.char(0xD800)))
print(pcall(utf8.char, -1))
print(pcall(utf8.char, 0x80000000))
print(pcall(utf8.char, "x"))
print(pcall(utf8.char, 65, 1.5))

print(utf8.charpattern == "[\0-\x7F\xC2-\xFD][\x80-\xBF]*", #utf8.charpattern)

local s = "a\u{E9}\u{20AC}\u{1F600}z"   -- a é € 😀 z: 1 + 2 + 3 + 4 + 1 bytes
print(#s, utf8.len(s), utf8.len(""), utf8.len("abc"))
print(utf8.codepoint(s))
print(utf8.codepoint(s, 1, -1))
print(utf8.codepoint(s, 2), utf8.codepoint(s, 4), utf8.codepoint(s, 7), utf8.codepoint(s, -1))
print(utf8.codepoint(s, 2, 7))
print(select("#", utf8.codepoint(s, 3, 2)), select("#", utf8.codepoint("", 1, 0)))
print(pcall(utf8.codepoint, s, 3))
print(pcall(utf8.codepoint, s, 20))
print(pcall(utf8.codepoint, s, 1, 20))
print(pcall(utf8.codepoint, "\xFF"))
print(pcall(utf8.codepoint, "\xC0\x80"))
print(pcall(utf8.codepoint, "\xED\xA0\x80"))
print(utf8.codepoint("\xED\xA0\x80", 1, 1, true), utf8.codepoint("\xF4\x90\x80\x80", 1, 1, true))
print(pcall(utf8.codepoint, "\xF4\x90\x80\x80"))
print(utf8.codepoint(utf8.char(0x7FFFFFFF), 1, 1, true))

-- len with ranges and invalid bytes.
print(utf8.len(s, 2), utf8.len(s, 4), utf8.len(s, -1), utf8.len(s, 1, 1), utf8.len(s, 1, 2))
print(utf8.len(s, 3))
print(utf8.len("abc\xFFdef"))
print(utf8.len("\xE2\x82"), utf8.len("ok\x80"))
print(utf8.len("\xED\xA0\x80"), utf8.len("\xED\xA0\x80", 1, -1, true))
print(utf8.len("\xC0\x80"), utf8.len("\xF8\x88\x80\x80\x80", 1, -1, true))
print(utf8.len("abc", 4), utf8.len("abc", 5 - 1, 3), utf8.len("abc", 1, 0))
print(pcall(utf8.len, "abc", 5))
print(pcall(utf8.len, "abc", 0))
print(pcall(utf8.len, "abc", 1, 4))
print(pcall(utf8.len))

return utf8.len("\u{48}\u{49}"), utf8.char(0x4C, 0x75, 0x61)
