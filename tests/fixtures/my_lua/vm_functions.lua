-- Functions and calls through the VM (stage 5): arguments adjusted to the
-- parameters, multiple results, varargs, tail calls, recursion and nested
-- calls. Each part returns its values joined in one string, so that no
-- results are cut.
local function show(v)
  if v == nil then return "nil" end
  if v == true then return "true" end
  if v == false then return "false" end
  return v
end

-- The first n values, joined with commas.
local function list(n, first, ...)
  if n == 0 then return "" end
  if n == 1 then return show(first) end
  return show(first) .. "," .. list(n - 1, ...)
end

local function three() return 1, 2, 3 end
local function none() end
local function id(...) return ... end

local function pick(a, b, c) return a, b, c end
local function sum(...)
  local a, b, c, d = ...
  return (a or 0) + (b or 0) + (c or 0) + (d or 0)
end
local function rest(first, ...) return ... end

-- Tail calls run in constant space: this would overflow the stack otherwise.
local function loop(n, acc)
  if n == 0 then return acc end
  return loop(n - 1, acc + 1)
end

local function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end

local function vararg_positions(...)
  local a, b = ...
  local x = (...)                -- parentheses: one value
  return a, b, x, ...
end

local function adjust()
  local a, b, c, d = three()     -- d gets nil
  local e, f = three(), 10       -- three() cut to one value
  local g = (three())            -- one value
  return a, b, c, d, e, f, g
end

-- A vararg function calling another with all of its arguments, through a
-- tail call.
local function forward(...) return pick(...) end

local function many(...)
  return ..., ...                -- the first ... is cut to one value
end

return list(3, pick(three())), list(3, pick(three(), 4)), list(3, pick(none())),
  list(3, pick(id(7, 8))), sum(1, 2), sum(three()), sum(), list(2, rest(1, 2, 3)),
  list(2, id()), list(2, id(nil, nil)), loop(100000, 0), fact(20),
  list(5, vararg_positions(5, 6, 7)), list(7, adjust()), list(2, id(id(id(1, 2)))),
  list(3, forward(9, 8, 7, 6)), list(4, many(1, 2, 3))
