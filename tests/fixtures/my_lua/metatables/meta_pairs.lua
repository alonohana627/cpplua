-- __pairs and ipairs with __index (stage 7): pairs(t) calls __pairs(t) and
-- uses its first three results; next ignores it. ipairs reads t[i] with
-- __index, so it walks a proxy, and stops at the first nil.

-- __pairs giving a deterministic order: the keys in a list.
local ordered = {}
ordered.__pairs = function(t)
  local i = 0
  return function(_, _)
    i = i + 1
    local k = t.__keys[i]
    if k ~= nil then return k, t.__data[k] end
  end, t, nil
end
local o = setmetatable({__keys = {"z", "a", "m"}, __data = {z = 26, a = 1, m = 13}}, ordered)
for k, v in pairs(o) do print(k, v) end

-- The three values returned by __pairs are passed through.
local state_seen, init_seen
local custom = setmetatable({}, {__pairs = function(t)
  return function(s, c)
    state_seen, init_seen = s, c
    return nil
  end, "state", "init"
end})
for _ in pairs(custom) do end
print(state_seen, init_seen)

-- next does not use __pairs.
local hidden = setmetatable({real = 1}, {__pairs = function() return function() end, nil, nil end})
local count = 0
for _ in pairs(hidden) do count = count + 1 end
print(count, next(hidden))

-- A __pairs that hides private fields (counting, not printing in order).
local private = setmetatable({_secret = 1, a = 1, b = 2, _x = 3}, {__pairs = function(t)
  local function iter(tbl, k)
    local nk, nv = next(tbl, k)
    while nk ~= nil and (nk == "_secret" or nk == "_x") do nk, nv = next(tbl, nk) end
    return nk, nv
  end
  return iter, t, nil
end})
local visible, total = 0, 0
for k, v in pairs(private) do visible = visible + 1 total = total + v end
print(visible, total)

-- ipairs on a proxy with __index.
local backing = {"a", "b", "c"}
local proxy = setmetatable({}, {__index = backing})
local parts = ""
for i, v in ipairs(proxy) do parts = parts .. i .. v .. " " end
print(parts, rawlen(proxy))

-- ipairs with an __index function that produces values up to 5.
local gen = setmetatable({}, {__index = function(_, i) if i <= 5 then return i * 10 end end})
local s = 0
for _, v in ipairs(gen) do s = s + v end
print(s)

-- Real items come first, __index fills the rest.
local partial = setmetatable({1, 2}, {__index = function(_, i) if i <= 4 then return -i end end})
parts = ""
for _, v in ipairs(partial) do parts = parts .. v .. " " end
print(parts)

-- pairs does not check its argument's type (only that there is one): it
-- returns next, and next then fails in the loop.
local f, st = pairs(nil)
print(f == next, st, pcall(pairs))
print(pcall(function() for _ in pairs(nil) do end end))
print(pcall(ipairs))

return count, s
