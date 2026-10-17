-- __call (stage 7): calling a table with a __call metamethod calls it with
-- the table first and then the arguments, and all its results come back;
-- callable objects work in a generic for, with pcall and as __index.

local counter = setmetatable({count = 0}, {
  __call = function(self, by)
    self.count = self.count + (by or 1)
    return self.count
  end,
})
print(counter(), counter(5), counter.count)

-- Multiple arguments and results.
local multi = setmetatable({}, {__call = function(self, ...)
  return select("#", ...), ...
end})
print(multi())
print(multi(1, nil, 3))
local packed = {multi("a", "b")}
print(#packed, packed[1], packed[2], packed[3])

-- The first argument is the table itself.
local who = setmetatable({name = "me"}, {__call = function(self) return self.name end})
print(who())

-- A class whose instances are made by calling the class.
local Point = setmetatable({}, {__call = function(cls, x, y)
  return setmetatable({x = x, y = y}, cls)
end})
Point.__index = Point
function Point:sum() return self.x + self.y end
local p = Point(3, 4)
print(p.x, p.y, p:sum(), getmetatable(p) == Point)

-- A callable object as a generic for iterator.
local range = setmetatable({}, {__call = function(_, limit, i)
  if i < limit then return i + 1 end
end})
local seen = 0
for i in range, 5, 0 do seen = seen + i end
print(seen)

-- Callable objects through pcall, in tail calls and as methods.
print(pcall(counter, 10))
local function tail() return counter(100) end
print(tail())
local holder = {f = who}
print(holder.f())

-- __call can itself be a callable table: inner gets (inner, outer, "x").
local inner, outer
inner = setmetatable({}, {__call = function(self, a, b) return self == inner, a == outer, b end})
outer = setmetatable({}, {__call = inner})
print(outer("x"))

-- A table without __call is not callable.
local plain = setmetatable({}, {})
print(pcall(plain))
local ok, msg = pcall(function() local t = {} t() end)
print(ok, msg)

return counter.count, p:sum()
