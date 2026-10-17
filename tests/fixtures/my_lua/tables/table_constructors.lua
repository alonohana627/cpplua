-- Table constructors (stage 6): positional items, named fields, [expr] = keys,
-- mixed and nested tables, a call or ... as the last item expanding to all its
-- values (and to one value anywhere else), more than 50 positional items (so
-- several SETLIST batches), and nil holes.

local empty = {}
print(#empty, next(empty))

local pos = {10, 20, 30}
print(#pos, pos[1], pos[2], pos[3], pos[4])

local named = {x = 1, y = "two", z = true}
print(named.x, named.y, named.z, named.w)

local k = "computed"
local keyed = {[1 + 1] = "two", [k] = "c", ["with space"] = 5, [true] = "yes"}
print(keyed[2], keyed.computed, keyed["with space"], keyed[true], #keyed == 0 or #keyed == 2)

-- Positional items count from 1 no matter where the named ones are.
local mixed = {"a", x = "X", "b", [10] = "ten", "c"; y = "Y"; "d"}
print(mixed[1], mixed[2], mixed[3], mixed[4], mixed.x, mixed.y, mixed[10], #mixed)

-- A later positional item wins over an earlier [n] = key for the same slot.
local clash = {[1] = "keyed", "positional"}
print(clash[1])

local nested = {name = "outer", inner = {name = "inner", deeper = {1, 2, {3}}}}
print(nested.name, nested.inner.name, nested.inner.deeper[3][1], #nested.inner.deeper)

-- A call as the last item gives all its values; anywhere else just the first.
local function three() return 1, 2, 3 end
local last = {three()}
local middle = {three(), 10}
local paren = {(three())}
print(#last, last[3], #middle, middle[1], middle[2], #paren)

local function none() end
local after_none = {none()}
local first_none = {none(), 5}
print(#after_none, first_none[1], first_none[2])

-- ... in a constructor.
local function pack(...) return {...}, select("#", ...) end
local p, n = pack("a", "b", "c")
print(n, #p, p[1], p[3])
local p2, n2 = pack()
print(n2, #p2)
local function first_and_rest(...) return {..., "end"} end
local fr = first_and_rest(7, 8, 9)
print(fr[1], fr[2], fr[3])

-- More than 50 positional items: several batches.
local big = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
  61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
  81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
  101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
  117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
  label = "big",
}
local sum = 0
for i = 1, #big do sum = sum + big[i] end
print(#big, big[1], big[50], big[51], big[100], big[130], big[131], sum, big.label)

-- Many items ending with a call: the call's values follow item 60.
local tail = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
  three()
}
print(#tail, tail[60], tail[61], tail[63])

-- nil holes: the values around them are kept.
local holes = {1, nil, 3, nil, 5}
print(holes[1], holes[2], holes[3], holes[4], holes[5])
local trailing_nils = {1, 2, nil, nil}
print(trailing_nils[1], trailing_nils[2], trailing_nils[3])

-- A constructor in a loop makes a fresh table every time.
local list = {}
for i = 1, 3 do list[i] = {i, i * i} end
print(list[1] ~= list[2], list[2][2], list[3][2])

return #big, big[#big]
