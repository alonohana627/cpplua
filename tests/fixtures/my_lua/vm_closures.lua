-- Closures and upvalues through the VM (stage 5): shared upvalues, upvalues
-- that outlive their function, fresh variables in every loop iteration,
-- upvalues of upvalues, and closing on break, goto and return (E7).
local function show(v)
  if v == nil then return "nil" end
  if v == true then return "true" end
  if v == false then return "false" end
  return v
end

local function list(n, first, ...)
  if n == 0 then return "" end
  if n == 1 then return show(first) end
  return show(first) .. "," .. list(n - 1, ...)
end

-- Two closures sharing one variable (E7.2), which lives on after
-- `counter` returns (E7.3).
local function counter()
  local n = 0
  local function inc(step) n = n + (step or 1) return n end
  local function get() return n end
  return inc, get
end
local inc, get = counter()
inc() inc(10)
local shared = get()

-- Each iteration has its own variable (E7.4): the closures see 1, 2, 3.
local f1, f2, f3
for i = 1, 3 do
  local function f() return i end
  if i == 1 then f1 = f elseif i == 2 then f2 = f else f3 = f end
end

-- The same with a while loop and a local declared in the body.
local w1, w2
local k = 0
while k < 2 do
  k = k + 1
  local captured = k * 100
  if k == 1 then w1 = function() return captured end else w2 = function() return captured end end
end

-- An upvalue of an upvalue: three levels deep, all sharing `x`.
local function outer()
  local x = 1
  local function middle()
    local function inner()
      x = x * 2
      return x
    end
    return inner
  end
  return middle(), function() return x end
end
local deep, peek = outer()
deep() deep()

-- Closing when leaving a loop with break, and when a goto jumps back.
local b1, b2
for i = 1, 10 do
  local v = i
  if i == 1 then b1 = function() return v end end
  if i == 2 then b2 = function() return v end break end
end
local g1, g2
local round = 0
::top::
round = round + 1
do
  local r = round
  if round == 1 then g1 = function() return r end else g2 = function() return r end end
end
if round < 2 then goto top end

-- A recursive local function sees itself as an upvalue.
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end

-- Assigning to an upvalue from the closure and from outside.
local value = "a"
local function set(v) value = v end
local function read() return value end
set("b")
local after_set = read()
value = "c"

-- Upvalues in vararg functions and through tail calls.
local function make_adder(x) return function(...) local y = ... return x + y end end
local add5 = make_adder(5)
local function tail(n) return add5(n) end

return shared, list(3, f1(), f2(), f3()), list(2, w1(), w2()), list(2, deep(), peek()),
  list(2, b1(), b2()), list(2, g1(), g2()), fib(15), after_set, read(), tail(10)
