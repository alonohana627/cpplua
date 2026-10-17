-- Yields from deep recursion: tree walks, permutations and a 5000-level descent.
local function walk(node)
  if node == nil then return end
  walk(node.left)
  coroutine.yield(node.key)
  walk(node.right)
end
local function insert(node, k)
  if node == nil then return {key = k} end
  if k < node.key then node.left = insert(node.left, k) else node.right = insert(node.right, k) end
  return node
end
local root
for _, k in ipairs({50, 30, 70, 20, 40, 60, 80, 35, 65}) do root = insert(root, k) end
local keys = {}
for k in coroutine.wrap(function() walk(root) end) do keys[#keys + 1] = k end
print(table.concat(keys, " "))
-- permutations
local function permgen(a, n)
  n = n or #a
  if n <= 1 then coroutine.yield(a)
  else
    for i = 1, n do
      a[n], a[i] = a[i], a[n]
      permgen(a, n - 1)
      a[n], a[i] = a[i], a[n]
    end
  end
end
local count = 0
for p in coroutine.wrap(function() permgen({"a", "b", "c", "d"}) end) do
  count = count + 1
  if count <= 6 then print(table.concat(p)) end
end
print("permutations", count)
-- a deep descent that yields at every 1000th level going down and coming up
local function deep(n)
  if n == 0 then return coroutine.yield("bottom") end
  if n % 1000 == 0 then coroutine.yield("down", n) end
  local r = deep(n - 1)
  if n % 1000 == 0 then coroutine.yield("up", n) end
  return r + 1
end
local co = coroutine.create(deep)
local ok, a, b = coroutine.resume(co, 5000)
while coroutine.status(co) == "suspended" do
  print(a, b)
  ok, a, b = coroutine.resume(co, a == "bottom" and 0 or nil)
end
print(ok, a)
-- recursion across coroutines: each level is a new coroutine
local function nest(n)
  if n == 0 then return 0 end
  local co = coroutine.wrap(function() coroutine.yield(nest(n - 1) + 1) end)
  return co()
end
return nest(50)
