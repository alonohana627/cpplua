-- Constructs that exercise the compiler's choices (stage 5). This program
-- does not run yet (it uses tables and globals); its golden file is the
-- bytecode the real luac makes of it, which lua2 must match exactly.
local K <const> = 10            -- a compile-time constant: no register
local S <const> = "name"
local F <const> = 2.5
local N <const> = K * 2 + 1     -- folded
local x, y, z = 1, 2            -- z: LOADNIL
local a; local b                -- one LOADNIL for both

local t = { 1, 2, 3, n = N, [K] = S, ["key with spaces"] = F, x, y }
local big = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, ...
}

t.x, t[1], t[x] = x + K, K - x, F * x
t[x], x = x, t[x]               -- the conflict: the old x is saved first
print(t.n, big[#big], S .. K, N // 3, -K, K < x, x <= 3, 3 < x, x == "s")
print(2 // 0, 1 // 0.0, 7 % -3, 1 << 62, 2^-1, -(-1), ~5, not nil, #S)

local obj = { value = 1 }
function obj.method(self, n) return self.value + n end
function obj:other(n) return self:method(n) * 2 end
local r = obj:other(K)

local function counter()
  local n = 0
  return function(step) n = n + (step or 1) return n end
end

for i = 1, K do
  local captured = i
  t[i] = function() return captured + K end
  if i == 5 then break end
end

for k, v in pairs(t) do
  if type(v) == "function" then goto continue end
  print(k, v)
  ::continue::
end

do
  local h <close> = nil
  local u = 1
  repeat
    local w = u
    u = u + 1
    local f = function() return w end
  until w > 3
end

local function vararg(...)
  local first, second = ...
  return select("#", ...), first, second, ...
end

local s = "a" .. "b" .. x .. "c" .. y
local cond = x and y or z
local cmp = x > 1 and y >= 2 or x ~= y
while x < 100 and not cond do x = x * 2 end
return counter, vararg(1, 2, 3), s, cmp, r, t, big
