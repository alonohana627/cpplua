-- Small base functions (stage 9): _VERSION, _G, warn control messages and
-- its argument checks (warnings themselves go to stderr and are switched off
-- here), getmetatable with a __metatable field, setmetatable protection,
-- ipairs stopping at the first nil, and next with an explicit key.

print(_VERSION, type(_VERSION))
print(_G._G == _G, _G.print == print, _G._VERSION == _VERSION)
_G.new_global = "set through _G"
print(new_global)

-- warn: "@off" switches warnings off, so nothing reaches stderr.
warn("@off")
print(warn("this warning is not shown"))
print(warn("several ", "pieces ", 3))
print(pcall(warn))
print(pcall(warn, {}))
print(pcall(warn, "ok", nil))
warn("@on")
warn("@off")

-- getmetatable and the __metatable field.
local mt = {}
local obj = setmetatable({}, mt)
print(getmetatable(obj) == mt, getmetatable({}), getmetatable(1))
mt.__metatable = "locked"
print(getmetatable(obj))
print(pcall(setmetatable, obj, {}))
print(pcall(setmetatable, obj, nil))
print(getmetatable("abc").__index == string)
print(pcall(setmetatable, {}, 1))
print(pcall(setmetatable, {}))
print(setmetatable(setmetatable({}, {}), nil) ~= nil)

-- ipairs stops at the first nil, and respects __index.
local seen = {}
for i, v in ipairs({10, 20, nil, 40}) do seen[#seen + 1] = i .. "=" .. v end
print(table.concat(seen, " "))
local virtual = setmetatable({}, {__index = function(_, i) if i <= 3 then return i * i end end})
seen = {}
for i, v in ipairs(virtual) do seen[#seen + 1] = i .. "=" .. v end
print(table.concat(seen, " "))
print(pcall(ipairs))

-- next over an array part, by explicit keys.
local arr = {"a", "b", "c"}
print(next(arr), next(arr, 1), next(arr, 2), next(arr, 3))
print(next({}), pcall(next, {}, "missing"))

-- assert passes all its arguments through.
print(assert(1, 2, 3))
print(select("#", assert(true, nil, nil)))
print(pcall(assert, false))
print(pcall(assert, nil, "custom message"))
print(pcall(assert))

return _VERSION
