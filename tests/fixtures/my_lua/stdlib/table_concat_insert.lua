-- table.concat and table.insert (stage 9): concat with separators, ranges,
-- numbers (integers and floats), empty ranges, and errors naming the bad
-- index; insert appending and at positions 1..#t+1, and its errors for
-- positions out of bounds and wrong argument counts. __index/__newindex and
-- __len are honoured by both.

local t = {"a", "b", "c", "d"}
print(table.concat(t), table.concat(t, ", "), table.concat(t, "", 2), table.concat(t, "-", 2, 3))
print(table.concat(t, "-", 3, 2) == "", table.concat({}) == "", table.concat({}, "x") == "")
print(table.concat(t, "-", 4, 4), table.concat({1, 2.5, -3, 2 ^ 63}, " "))
print(table.concat({1, "two", 3}, 0), pcall(table.concat, t, "", -1, 1))
print(table.concat({[0] = "zero", "one"}, ",", 0, 1), table.concat({"x"}, "\0") == "x")
print(pcall(table.concat, {1, {}, 3}))
print(pcall(table.concat, {1, 2, nil, 4}, ",", 1, 4))
print(pcall(table.concat, {true}))
print(pcall(table.concat, t, {}))
print(pcall(table.concat, "abc"))
print(pcall(table.concat))

-- concat with metamethods.
local virtual = setmetatable({}, {
  __index = function(_, i) return "v" .. i end,
  __len = function() return 3 end,
})
print(table.concat(virtual, "+"))

-- insert.
local list = {}
table.insert(list, "x")
table.insert(list, "y")
table.insert(list, 1, "first")
table.insert(list, #list + 1, "last")
table.insert(list, 3, "middle")
print(#list, table.concat(list, " "))
table.insert(list, 1.0, "float pos")
print(table.concat(list, " "))
print(pcall(table.insert, list, 0, "zero"))
print(pcall(table.insert, list, #list + 2, "too far"))
print(pcall(table.insert, list, -1, "neg"))
print(pcall(table.insert, list, 1.5, "half"))
print(pcall(table.insert, list, 1, 2, 3))
print(pcall(table.insert, list))
print(pcall(table.insert, nil, 1))
print(pcall(table.insert, list, "x", 1))

-- insert returns nothing and nil can be inserted at the end.
print(select("#", table.insert(list, "z")), #list)
local holes = {1, 2, 3}
table.insert(holes, nil)
print(#holes)

-- insert into an empty table at position 1.
local empty = {}
table.insert(empty, 1, "only")
print(empty[1], #empty)

-- insert with __newindex sees each shifted element.
local log = {}
local proxy = setmetatable({}, {
  __len = function() return 2 end,
  __index = function(_, k) return "old" .. k end,
  __newindex = function(_, k, v) log[#log + 1] = k .. "=" .. v end,
})
table.insert(proxy, 1, "new")
print(table.concat(log, " "))

return table.concat(list, ","), #list
