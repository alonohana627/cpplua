-- Pattern character classes (stage 9): which of the ASCII characters 0..127
-- each class %a %c %d %g %l %p %s %u %w %x matches, their complements, the
-- dot, escaped magic characters, and classes inside sets. Printable members
-- are listed; the count covers all of them.

local all = {}
for c = 0, 127 do all[#all + 1] = string.char(c) end
local ascii = table.concat(all)

local function members(class)
  local count, shown = 0, {}
  for c in ascii:gmatch(class) do
    count = count + 1
    local b = c:byte()
    if b > 32 and b < 127 then shown[#shown + 1] = c end
  end
  return count, table.concat(shown)
end

for _, class in ipairs({"%a", "%c", "%d", "%g", "%l", "%p", "%s", "%u", "%w", "%x"}) do
  print(class, members(class))
end

-- Complements: each upper-case class matches the other characters.
for _, class in ipairs({"%A", "%C", "%D", "%G", "%L", "%P", "%S", "%U", "%W", "%X"}) do
  local count = members(class)
  print(class, count, count + members(class:lower()))
end

-- The space class in detail.
local spaces = {}
for c in ascii:gmatch("%s") do spaces[#spaces + 1] = c:byte() end
print("spaces", table.concat(spaces, " "))
local controls = {}
for c in ascii:gmatch("[%c%s]") do controls[#controls + 1] = c:byte() end
print("controls or spaces", #controls)

-- The dot matches everything, including "\0" and "\n".
print(members("."), ("\0\n"):match("^..$") == "\0\n")

-- Escaped magic characters match themselves.
local magic = "^$()%.[]*+-?"
local escaped = {}
for c in magic:gmatch(".") do escaped[#escaped + 1] = magic:find("%" .. c, 1) end
print(table.concat(escaped, " "))
print(("a.b"):match("%."), ("50%"):match("%d+%%"), ("x+y"):match("%+"))

-- %w and %a with non-letter punctuation.
print(("snake_case99"):match("%w+"), ("snake_case99"):match("[%w_]+"))
print(("Hex: 0xDEADbeef!"):match("0x(%x+)"))
print(("MiXeD"):gsub("%u", "_"), ("MiXeD"):gsub("%l", "_"))
print(("a1 b2\tc3\n"):gsub("%s", "."))
print(("{x=[1]}"):gsub("%p", ""))

return members("[%d%u]")
