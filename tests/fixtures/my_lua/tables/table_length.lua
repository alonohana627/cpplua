-- The length operator on tables (stage 6), only where it is well defined:
-- proper sequences while they grow and shrink, after t[#t] = nil, and the
-- empty table. # on strings still works.

local t = {}
print(#t)
for i = 1, 20 do
  t[#t + 1] = i * 10
end
print(#t, t[#t])

-- Shrinking from the end keeps a proper sequence.
for _ = 1, 5 do t[#t] = nil end
print(#t, t[#t], t[16])

-- Down to empty and up again.
while #t > 0 do t[#t] = nil end
print(#t, t[1])
t[1] = "again"
print(#t)

-- Constructors.
print(#{}, #{1}, #{1, 2, 3}, #{"a", "b", n = 2}, #{n = 1})

-- A sequence built with explicit keys, out of order.
local s = {}
s[3] = "c"
s[1] = "a"
s[2] = "b"
print(#s)

-- A sequence of 1000 built by appending, then shrunk to 1.
local seq = {}
for i = 1, 1000 do seq[#seq + 1] = i end
local sum = 0
for i = 1, #seq do sum = sum + seq[i] end
print(#seq, sum)
for _ = 1, 999 do seq[#seq] = nil end
print(#seq, seq[1])

-- Values of any type count, only nil ends a sequence.
local any = {false, 0, "", {}, print}
print(#any)

-- Strings.
local str = "hello"
print(#str, #"", #(str .. str), #"\0\0")

-- Lengths used in expressions.
local q = {1, 2, 3}
print(#q * 2, #q + #q, -#q, #q == 3)

return #t, #seq, #any
