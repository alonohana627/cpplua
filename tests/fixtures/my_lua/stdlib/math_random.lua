-- math.random and math.randomseed (stage 9): with a fixed seed the
-- xoshiro256** generator gives the same sequence every time: floats in
-- [0, 1), integers in [1, m], in [m, n], and math.random(0) returning a whole
-- random integer. Reseeding repeats the sequence; two-part seeds; the
-- interval errors.

local function floats(n)
  local out = {}
  for i = 1, n do out[i] = string.format("%.17g", math.random()) end
  return table.concat(out, " ")
end
local function ints(n, ...)
  local out = {}
  for i = 1, n do out[i] = math.random(...) end
  return table.concat(out, " ")
end

math.randomseed(42)
print(floats(4))
print(ints(10, 10))
print(ints(10, 5, 20))
print(ints(3, 0))
print(ints(5, -3, 3))
print(ints(3, math.mininteger, math.maxinteger))
print(ints(5, 1))
print(ints(4, 7, 7))

-- The same seed repeats the same sequence.
math.randomseed(42)
local first = floats(4)
math.randomseed(42)
print(first == floats(4))

-- Other seeds, including a float seed, a negative seed and two-part seeds.
for _, seed in ipairs({0, 1, 123456789, -1, math.maxinteger, math.mininteger}) do
  math.randomseed(seed)
  print(seed, ints(6, 100), ints(1, 0))
end
math.randomseed(3.0)
print("3.0", ints(6, 100))
math.randomseed(1, 2)
print("1,2", ints(6, 100), floats(1))
math.randomseed(1, 0)
print("1,0", ints(6, 100))
math.randomseed(0, 1)
print("0,1", ints(6, 100))

-- The results stay inside the interval and cover it.
math.randomseed(7)
local seen, outside = {}, 0
for _ = 1, 1000 do
  local r = math.random(3, 6)
  if r < 3 or r > 6 then outside = outside + 1 end
  seen[r] = true
end
print(outside, seen[3], seen[4], seen[5], seen[6])
local all_floats_ok = true
for _ = 1, 1000 do
  local r = math.random()
  if r < 0 or r >= 1 or math.type(r) ~= "float" then all_floats_ok = false end
end
print(all_floats_ok, math.type(math.random(5)), math.type(math.random(0)))

-- randomseed returns the two parts of the seed; with no arguments it seeds
-- randomly.
print(select("#", math.randomseed()), math.type((math.randomseed())))
print(math.randomseed(5))
print(math.randomseed(-7, 9))

-- Errors.
print(pcall(math.random, 0, -1))
print(pcall(math.random, 10, 1))
print(pcall(math.random, -5))
print(pcall(math.random, 1, 2, 3))
print(pcall(math.random, 1.5))
print(pcall(math.random, "x"))
print(pcall(math.randomseed, "x"))
print(pcall(math.randomseed, 3.5))
print(pcall(math.randomseed, 1, {}))

math.randomseed(2024)
return math.random(1000), math.random(1000)
