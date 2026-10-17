-- Tables as objects and data structures (stage 6), without metatables:
-- methods called with : and self, tables holding functions, a linked list,
-- a stack and a queue, a memoize cache and a word counter.

-- An object with methods.
local account = {balance = 0, owner = "ann"}
function account.deposit(self, amount) self.balance = self.balance + amount return self end
function account:withdraw(amount)
  if amount > self.balance then return false, "insufficient funds" end
  self.balance = self.balance - amount
  return true
end
function account:report() return self.owner .. ": " .. self.balance end
account:deposit(100):deposit(50)
print(account:report())
print(account:withdraw(500))
print(account:withdraw(30), account.balance)
print(account.report(account), account.report({owner = "bob", balance = 7}))

-- A constructor function making objects that share method functions.
local Point = {}
function Point.new(x, y) return {x = x, y = y, add = Point.add, str = Point.str} end
function Point.add(a, b) return Point.new(a.x + b.x, a.y + b.y) end
function Point.str(p) return "(" .. p.x .. ", " .. p.y .. ")" end
local p = Point.new(1, 2):add(Point.new(10, 20)):add(Point.new(100, 200))
print(p:str(), p.add == Point.add)

-- A table of functions: a dispatch table.
local ops = {
  ["+"] = function(a, b) return a + b end,
  ["-"] = function(a, b) return a - b end,
  ["*"] = function(a, b) return a * b end,
}
local function calc(a, op, b) return ops[op](a, b) end
print(calc(6, "+", 7), calc(6, "-", 7), calc(6, "*", 7))

-- A linked list.
local list = nil
for i = 1, 5 do list = {value = i, next = list} end
local node, parts = list, ""
while node do
  parts = parts .. node.value .. " "
  node = node.next
end
print(parts)
-- Reverse it in place.
local prev = nil
node = list
while node do
  local nxt = node.next
  node.next = prev
  prev, node = node, nxt
end
list = prev
parts = ""
node = list
while node do parts = parts .. node.value .. " " node = node.next end
print(parts)

-- A stack.
local stack = {n = 0}
function stack:push(v) self.n = self.n + 1 self[self.n] = v end
function stack:pop()
  if self.n == 0 then return nil end
  local v = self[self.n]
  self[self.n] = nil
  self.n = self.n - 1
  return v
end
for _, w in ipairs({"x", "y", "z"}) do stack:push(w) end
print(stack:pop(), stack:pop(), stack.n, stack:pop(), stack:pop(), stack.n)

-- A queue with two indices.
local queue = {first = 1, last = 0}
local function enqueue(q, v) q.last = q.last + 1 q[q.last] = v end
local function dequeue(q)
  if q.first > q.last then return nil end
  local v = q[q.first]
  q[q.first] = nil
  q.first = q.first + 1
  return v
end
for i = 1, 100 do enqueue(queue, i) end
local qsum = 0
for _ = 1, 60 do qsum = qsum + dequeue(queue) end
for i = 101, 110 do enqueue(queue, i) end
local rest = 0
while true do
  local v = dequeue(queue)
  if v == nil then break end
  rest = rest + v
end
print(qsum, rest, queue.first, queue.last)

-- A memoize cache.
local calls = 0
local function memoize(f)
  local cache = {}
  return function(n)
    local v = cache[n]
    if v == nil then v = f(n) cache[n] = v end
    return v
  end
end
local fib
fib = memoize(function(n)
  calls = calls + 1
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end)
print(fib(80), calls)
print(fib(80), fib(40), calls)

-- Counting words, then checking counts (not the traversal order).
local words = {"the", "cat", "and", "the", "hat", "and", "the", "bat"}
local counts, distinct = {}, 0
for _, w in ipairs(words) do
  if counts[w] == nil then distinct = distinct + 1 counts[w] = 0 end
  counts[w] = counts[w] + 1
end
print(distinct, counts.the, counts["and"], counts.cat, counts.dog)
local total = 0
for _, c in pairs(counts) do total = total + c end
print(total == #words)

-- A set.
local set = {}
for _, w in ipairs(words) do set[w] = true end
print(set.cat, set.dog, set.hat and "yes" or "no")

return account.balance, p.x, fib(10)
