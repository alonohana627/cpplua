-- Stack overflows caught by pcall (stage 8): unbounded Lua recursion gives
-- "stack overflow"; unbounded recursion through metamethods (which nest
-- calls inside the interpreter, like C calls in the real Lua) gives
-- "C stack overflow"; __index and __newindex chains of tables that loop
-- are detected without recursion. The program goes on normally afterwards.

local function rec(n) return 1 + rec(n + 1) end
print(pcall(rec, 1))

-- The same through a method and through mutual recursion.
local obj = {}
function obj:loop() return self:loop() + 1 end
print(pcall(obj.loop, obj))
local ping
local function pong(n) return ping(n) + 1 end function ping(n) return pong(n) + 1 end
print(pcall(ping, 0))

-- Recursion through an __index function.
local t = setmetatable({}, {})
getmetatable(t).__index = function(self, k) return self[k] end
print(pcall(function() return t.x end))

-- Through __concat and __add metamethods.
local cat = setmetatable({}, {__concat = function(x, y) return x .. y end})
print(pcall(function() return cat .. "s" end))
local num = setmetatable({}, {__add = function(x, y) return x + y end})
print(pcall(function() return num + 1 end))

-- __index and __newindex chains of tables that form a loop.
local u = setmetatable({}, {})
getmetatable(u).__index = u
print(pcall(function() return u.x end))
local a = setmetatable({}, {})
local b = setmetatable({}, {__index = a})
getmetatable(a).__index = b
print(pcall(function() return a.x end))
local w = setmetatable({}, {})
getmetatable(w).__newindex = w
print(pcall(function() w.x = 1 end))

-- A deep but finite recursion is fine.
local function depth(n) if n == 0 then return 0 end return 1 + depth(n - 1) end
print(depth(10000))

-- A deep chain of __index tables that is not a loop is fine too.
local chain = {value = "bottom"}
for _ = 1, 100 do chain = setmetatable({}, {__index = chain}) end
print(chain.value)

-- After all that, everything still works.
print(pcall(rec, 1))
return "recovered"
