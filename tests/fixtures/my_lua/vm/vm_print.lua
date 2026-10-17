-- print and global variables (the start of the standard library): print
-- writes its arguments the way tostring does, separated by tabs; globals
-- live in the globals table, _ENV.
print("hello", 1, 2.5, -0.0, 1e100, 2^63, nil, true, false)
print()
print("")
print(1, nil, 3)

count = 0
local function bump(by) count = count + by return count end
bump(1) bump(10)
print("count", count, _ENV.count, _ENV["count"])

function greet(name) return "hi " .. name end
print(greet("cpplua"), greet "you")

print(undefined_global, undefined_global == nil)
count = nil
print(count)

-- Many globals, so the globals table grows, each a fresh string.
for i = 1, 100 do
  _ENV["g" .. i] = "value " .. i
end
print(g1, g50, g100, g101)

-- A global updated in a loop with new strings while the collector runs.
local total = 0
for i = 1, 2000 do
  latest = "item " .. i
  total = total + #latest
end
print(latest, total)

local p = print
p("print through a local")
print(print == p, print ~= greet)
return "done", count
