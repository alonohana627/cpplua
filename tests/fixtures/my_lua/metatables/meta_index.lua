-- __index (stage 7): a table (inheritance chains, classes with methods) or a
-- function receiving the table and the key; used only for keys that are
-- absent, for string and integer keys alike; rawget bypasses it.

-- __index as a table: defaults.
local defaults = {color = "red", size = 10}
local item = setmetatable({size = 3}, {__index = defaults})
print(item.color, item.size, item.weight, rawget(item, "color"), rawget(item, "size"))

-- A chain of three levels.
local base = {kind = "base", level = 1}
local middle = setmetatable({level = 2}, {__index = base})
local top = setmetatable({}, {__index = middle})
print(top.kind, top.level, top.missing, rawget(top, "level"))
middle.level = nil
print(top.level)
base.kind = "changed"
print(top.kind)

-- A class with methods and a subclass.
local Animal = {}
Animal.__index = Animal
function Animal.new(name, sound)
  return setmetatable({name = name, sound = sound}, Animal)
end
function Animal:speak() return self.name .. " says " .. self.sound end
function Animal:kind() return "animal" end

local Dog = setmetatable({}, {__index = Animal})
Dog.__index = Dog
function Dog.new(name) local d = Animal.new(name, "woof") return setmetatable(d, Dog) end
function Dog:kind() return "dog" end
function Dog:fetch() return self.name .. " fetches" end

local cat = Animal.new("cat", "meow")
local rex = Dog.new("rex")
print(cat:speak(), cat:kind(), cat.fetch)
print(rex:speak(), rex:kind(), rex:fetch())
print(getmetatable(rex) == Dog, rawget(rex, "speak"), Dog.speak == Animal.speak)

-- __index as a function: receives the table and the key.
local log = {}
local computed = setmetatable({}, {
  __index = function(t, k)
    log[#log + 1] = k
    if type(k) == "number" then return k * k end
    return "<" .. tostring(k) .. ">"
  end,
})
print(computed[4], computed.name, computed[2.5], computed[true])
computed[4] = "stored"
print(computed[4], #log, log[1], log[2])

-- The function receives the original table, not the metatable.
local seen_self
local proxy = setmetatable({}, {__index = function(t, k) seen_self = t return k end})
local _ = proxy.anything
print(seen_self == proxy)

-- __index on missing integer keys of a sequence.
local seq = setmetatable({10, 20, 30}, {__index = function(_, i) return -i end})
print(seq[1], seq[3], seq[4], seq[0], seq[-2], #seq)

-- __index reached through a chain where a function is at the end.
local fallback = setmetatable({}, {__index = function(_, k) return "fallback " .. k end})
local chained = setmetatable({own = "own"}, {__index = fallback})
print(chained.own, chained.other)

-- rawget never calls __index.
print(rawget(computed, "nothing"), rawget(seq, 4), rawget(seq, 1))

-- A memoizing __index: the function stores what it computes.
local calls = 0
local squares = setmetatable({}, {__index = function(t, n)
  calls = calls + 1
  local v = n * n
  rawset(t, n, v)
  return v
end})
print(squares[12], squares[12], squares[13], calls)

-- __index set to the metatable itself (the usual class idiom), on numbers.
local Vec = {}
Vec.__index = Vec
function Vec:len2() return self[1] * self[1] + self[2] * self[2] end
print(setmetatable({3, 4}, Vec):len2())

-- Changing __index after setmetatable takes effect at once.
local mt = {}
local late = setmetatable({}, mt)
print(late.x)
mt.__index = {x = "now"}
print(late.x)

return top.kind, rex:kind(), computed[3]
