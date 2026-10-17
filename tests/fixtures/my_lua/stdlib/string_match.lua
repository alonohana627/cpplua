-- string.match and string.gmatch (stage 9): whole-match results without
-- captures, several captures, position captures, nested captures, init for
-- match (including negative), gmatch with and without captures, '^' in
-- gmatch patterns (a plain character there), the init argument of gmatch (new in 5.4), and empty
-- matches advancing by one character.

print(string.match("hello 123 world", "%d+"))
print(string.match("hello 123 world", "(%a+) (%d+)"))
print(string.match("hello", "()ll()"))
print(string.match("hello", "(h(el)(l))o"))
print(string.match("hello", "xyz"), string.match("", ".*") == "", string.match("", "x*") == "")
print(string.match("hello", "l", -2), string.match("hello", ".", -1), string.match("hello", "^h", 2))
print(string.match("key=val", "^(%w+)=(%w*)$"))
print(string.match("key=", "^(%w+)=(%w*)$"))
print(string.match("  padded  ", "^%s*(.-)%s*$") .. "|")
print(string.match("[[nested]]", "%[(%b[])%]"))
print(string.match("f(a(b)c)d", "%b()"))
print(string.match("THE (quick) fox", "%((%a+)%)"))
print(string.match("abc", "()"), string.match("abc", "()", 4), string.match("abc", "()", 10))
print(string.match(2024, "^%d%d"), string.match("x", "()x()"))

-- gmatch without captures yields whole matches.
local words = {}
for w in string.gmatch("one two  three", "%a+") do words[#words + 1] = w end
print(#words, table.concat(words, ","))

-- With captures it yields the captures.
local pairs_found = {}
for k, v in string.gmatch("a=1, b=2, c=3", "(%w+)=(%w+)") do
  pairs_found[#pairs_found + 1] = k .. ":" .. v
end
print(table.concat(pairs_found, " "))

-- Position captures.
local positions = {}
for p in string.gmatch("banana", "()a") do positions[#positions + 1] = p end
print(table.concat(positions, " "))

-- Empty matches: one at each position, including the end.
local empties = {}
for p in string.gmatch("abc", "()") do empties[#empties + 1] = p end
print(table.concat(empties, " "))
local stars = {}
for m in string.gmatch("aab", "a*") do stars[#stars + 1] = "[" .. m .. "]" end
print(table.concat(stars))

-- gmatch has no anchor: a '^' in its pattern is an ordinary character.
local anchored = {}
for m in string.gmatch("aaa", "^a") do anchored[#anchored + 1] = m end
print(#anchored)
for m in string.gmatch("^a^a", "^a") do anchored[#anchored + 1] = m end
print(#anchored, anchored[1])

-- The init argument.
local from3 = {}
for m in string.gmatch("abcdef", ".", 3) do from3[#from3 + 1] = m end
print(table.concat(from3))
local from_neg = {}
for m in string.gmatch("abcdef", ".", -2) do from_neg[#from_neg + 1] = m end
print(table.concat(from_neg))
local past = 0
for _ in string.gmatch("abc", ".", 10) do past = past + 1 end
print(past)

-- The iterator can be called by hand and ends with nil.
local it = string.gmatch("x y", "%a")
print(it(), it(), it(), it())

-- Splitting lines, keeping empty ones.
local lines = {}
for line in string.gmatch("first\n\nthird\n", "([^\n]*)\n") do lines[#lines + 1] = "<" .. line .. ">" end
print(table.concat(lines))

print(pcall(string.match, "abc"))
print(pcall(string.gmatch, "abc"))
print(pcall(string.match, "abc", "(", 1))

return string.match("version 5.4.9", "(%d+)%.(%d+)%.(%d+)")
