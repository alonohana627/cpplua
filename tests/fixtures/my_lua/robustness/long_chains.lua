-- Expressions whose left side is a long chain compile in a loop, never
-- recursing once per link (issue #11): sums, calls, indexing, methods, and
-- `and`, `or` and `==` chains, each 20000 links long.
local n = 20000
print(load("return 1" .. ("+1"):rep(n))())
print(load("return 'x'" .. (" == 'x'"):rep(n))())
print(load("return 1" .. (" and 1"):rep(n))(), load("return nil" .. (" or nil"):rep(n) .. " or 2")())
local f
f = function() return f end
print(load("local f = ... return f" .. ("()"):rep(n) .. " == f")(f))
local t = {}
t.a = t
print(load("local t = ... return t" .. (".a"):rep(n) .. " == t")(t))
local o = {}
function o:m() return self end
print(load("local o = ... return o" .. (":m()"):rep(n) .. " == o")(o))
print(load("local t = ... return t" .. ("['a']"):rep(n) .. ".a == t")(t))
