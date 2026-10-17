-- Tables as garbage (stage 6): many tables created and dropped in loops,
-- tables kept alive only through other tables, cycles between tables, and one
-- big table of 100000 integers. The results must not depend on when the
-- collector runs.

-- Short-lived tables.
local made = 0
for i = 1, 20000 do
  local t = {i, i + 1, name = "t" .. (i % 10)}
  made = made + t[2] - t[1]
end
print(made)

-- A tree of tables kept only through its root.
local function build(depth)
  if depth == 0 then return {leaf = true} end
  return {left = build(depth - 1), right = build(depth - 1), depth = depth}
end
local function count(node)
  if node.leaf then return 1 end
  return 1 + count(node.left) + count(node.right)
end
local tree = build(12)
for _ = 1, 3 do build(10) end -- garbage trees
print(count(tree), tree.depth, tree.left.left.depth)

-- Cycles: each table refers to the next, the last back to the first.
local ring = {}
for i = 1, 1000 do ring[i] = {id = i} end
for i = 1, 1000 do ring[i].next = ring[i % 1000 + 1] end
local node, steps = ring[1], 0
repeat node = node.next steps = steps + 1 until node == ring[1]
ring = {node} -- drop all but one reference into the cycle
node = node.next.next
print(steps, node.id, node.next.id)

-- Cycles that become garbage.
for _ = 1, 200 do
  local a, b = {}, {}
  a.other, b.other = b, a
  a.self = a
end

-- Tables as keys and values of other tables, replaced repeatedly.
local registry = {}
for i = 1, 5000 do
  local key = {}
  registry[i % 50 + 1] = {key = key, [key] = i}
end
local rsum = 0
for i = 1, 50 do
  local entry = registry[i]
  rsum = rsum + entry[entry.key]
end
print(rsum)

-- A big table of 100000 integers.
local big = {}
for i = 1, 100000 do big[i] = i end
local sum = 0
for i = 1, #big do sum = sum + big[i] end
print(#big, sum)
local psum = 0
for _, v in ipairs(big) do psum = psum + v end
print(psum == sum)

-- Strings as values in a large table.
local strs = {}
for i = 1, 3000 do strs[i] = "s" .. i end
local len = 0
for i = 1, #strs do len = len + #strs[i] end
print(len, strs[3000])

collectgarbage()
print(count(tree), #big, strs[1])
return sum, collectgarbage("count") > 0
