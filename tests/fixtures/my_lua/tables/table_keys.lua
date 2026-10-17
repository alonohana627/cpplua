-- Keys of every type (stage 6): integers, floats with an integer value (they
-- are normalized, so t[1.0] is t[1], also for 2^53), non-integral floats,
-- short and long strings, booleans, tables and functions as keys. Absent keys
-- read nil and assigning nil removes an entry.

local t = {}

t[1] = "one"
print(t[1], t[1.0], t[2 / 2], t[3 // 3])
t[2.0] = "two"
print(t[2], t[2.0], math_is_missing)
t[-0.0] = "zero"
print(t[0], t[0.0], t[-0])

local big = 2 ^ 53            -- a float with an integer value
t[big] = "two^53"
print(t[9007199254740992], t[big], t[1 << 53])
t[2 ^ 63] = "float 2^63"      -- does not fit an integer: stays a float key
print(t[2 ^ 63], t[math_is_missing or 9223372036854775807])

t[1.5] = "one and a half"
t[-2.25] = "minus"
print(t[1.5], t[3 / 2], t[-2.25], t[1], t[2])

-- Integer keys and string keys are different keys.
t["1"] = "string one"
print(t[1], t["1"], t[10 // 10 .. ""])

-- Short and long strings: equal contents are equal keys however they were made.
local short = "name"
t[short] = "short"
local long = "this string is longer than forty bytes, so it is a long string"
t[long] = "long"
local built = "this string is longer than forty bytes, " .. "so it is a long string"
print(t.name, t["na" .. "me"], t[long], t[built], #long)
local long2 = "another long string key, also over forty characters in length!!"
t[long2] = 1
t[long2] = t[long2] + 1
print(t[long2], t["another long string key, also over forty characters in length!!"])
t[""] = "empty"
print(t[""])

-- Booleans.
t[true] = "T"
t[false] = "F"
print(t[true], t[false], t[1 == 1], t[1 == 2])

-- Tables and functions as keys: by identity.
local k1, k2 = {}, {}
local function f() end
local function g() end
t[k1] = "table one"
t[k2] = "table two"
t[f] = "function f"
t[print] = "builtin print"
print(t[k1], t[k2], t[{}], t[f], t[g], t[print])

-- Absent keys read nil; nil assignment removes; reassigning restores.
print(t.missing, t[12345], t[-1], t[0.5])
t[k1] = nil
t.name = nil
t[1.5] = nil
print(t[k1], t.name, t[1.5], t[k2])
t.name = "back"
print(t.name)

-- Counting entries with next (the order does not matter for a count).
local count = 0
for _ in pairs(t) do count = count + 1 end
print("entries", count)

-- Values of every type.
local values = {n = nil, b = false, i = 3, f = 0.5, s = "s", t = {}, fn = f}
print(values.n, values.b, values.i, values.f, values.s, type(values.t), type(values.fn))

-- Updating through different spellings of the same key.
local u = {}
u[3] = 1
u[3.0] = u[3] + 1
u[6 / 2] = u[3.0] + 1
print(u[3])

return t[1], t[2.0], t[true], t[long]
