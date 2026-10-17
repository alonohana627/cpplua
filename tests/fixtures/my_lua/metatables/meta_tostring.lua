-- __tostring and __name (stage 7): tostring and print use __tostring, whose
-- result must be a string; __name is only used when there is no __tostring,
-- and then the text has an address in it, so this program only checks its
-- type. tostring of plain values for reference.

local Point = {}
Point.__tostring = function(p) return "Point(" .. p.x .. ", " .. p.y .. ")" end
local p = setmetatable({x = 1, y = 2}, Point)
print(p)
print(tostring(p), "in " .. tostring(p))
print(p, p, "end")

-- __tostring gets the object; it can use other metamethods.
local Named = {__index = {greeting = "hello"}}
Named.__tostring = function(self) return self.greeting .. " " .. self.name end
print(setmetatable({name = "world"}, Named))

-- __tostring wins over __name.
local both = setmetatable({}, {__tostring = function() return "custom" end, __name = "MyType"})
print(both, tostring(both))

-- With __name only, tostring gives "MyType: <address>": check only its type.
local named_only = setmetatable({}, {__name = "MyType"})
print(type(tostring(named_only)))
-- __name that is not a string is ignored.
local bad_name = setmetatable({}, {__name = 42})
print(type(tostring(bad_name)))

-- A __tostring that does not return a string is an error, but a number
-- counts as a string (it is returned as is, so print shows it).
local wrong = setmetatable({}, {__tostring = function() return 42 end})
print(pcall(tostring, wrong))
local wrong_nil = setmetatable({}, {__tostring = function() end})
print(pcall(tostring, wrong_nil))
print(pcall(print, wrong))

-- Errors inside __tostring propagate.
local failing = setmetatable({}, {__tostring = function() error("no text", 0) end})
print(pcall(tostring, failing))

-- tostring of the basic values.
print(tostring(nil), tostring(true), tostring(false), tostring(12), tostring(-0.0))
print(tostring(1e15), tostring(1e16), tostring(2^63), tostring(-7 // 2), tostring(7 / 2))
print(tostring("str"), tostring(""), pcall(tostring))

-- A list type whose __tostring calls tostring on its items.
local List = {}
List.__tostring = function(l)
  local s = "{"
  for i, v in ipairs(l) do
    if i > 1 then s = s .. ", " end
    s = s .. tostring(v)
  end
  return s .. "}"
end
local function list(...) return setmetatable({...}, List) end
print(list(1, "two", p, list(true, false), list()))

return tostring(p), p.x
