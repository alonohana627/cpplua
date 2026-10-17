-- Control flow through the VM (stage 5): if, while, repeat, numeric for
-- (integer and float loops, limits clipped, negative steps), goto, break,
-- and the short-circuit operators. Each part returns its values joined in
-- one string, so that no results are cut (only the last call in a list
-- gives all of its values).
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

local function classify(n)
  if n < 0 then
    return "negative"
  elseif n == 0 then
    return "zero"
  elseif n < 10 then
    return "small"
  else
    return "large"
  end
end

local function sum_while(n)
  local s, k = 0, 1
  while k <= n do
    s = s + k
    k = k + 1
  end
  return s
end

local function count_repeat(n)
  local k = 0
  repeat
    local twice = k * 2
    k = k + 1
  until twice >= n -- the condition sees the body's local
  return k
end

local function loops()
  local up, down, by3, floats, clipped, empty, wide = 0, 0, 0, 0.0, 0, 0, 0
  for v = 1, 10 do up = up + v end
  for v = 10, 1, -1 do down = down * 2 + v end
  for v = 1, 20, 3 do by3 = by3 + v end
  for v = 0.5, 2.0, 0.25 do floats = floats + v end
  for v = 1, 3.7 do clipped = clipped + v end
  for v = 5, 1 do empty = empty + 1 end
  for v = 9223372036854775806, 9223372036854775807 do wide = wide + 1 end
  local last
  for v = 1, 3 do v = v * 10 last = v end -- changing the variable does not change the loop
  local huge = 0
  for v = 1, 1 / 0 do huge = huge + 1 if huge == 3 then break end end
  local strings = 0
  for v = "1", "3" do strings = strings + v end -- numeric strings are converted
  return up, down, by3, floats, clipped, empty, wide, last, huge, strings
end

local function jumps()
  local found
  for x = 1, 100 do
    if x * x > 50 then
      found = x
      break
    end
  end
  local n = 0
  ::again::
  n = n + 1
  if n < 5 then goto again end
  local odd = 0
  for k = 1, 10 do
    if k % 2 == 0 then goto continue end
    odd = odd + k
    ::continue::
  end
  local nested = 0
  for a = 1, 3 do
    for b = 1, 3 do
      if b > a then break end
      nested = nested + 1
    end
  end
  local w = 0
  while true do
    w = w + 1
    if w >= 4 then break end
  end
  return found, n, odd, nested, w
end

local function logic(x, y)
  return x and y, x or y, not x, x and nil, nil or false, false or nil,
    (x and 1 or 2), (y and 1 or 2), x == y and "same" or "different"
end

return classify(-5), classify(0), classify(3), classify(42),
  sum_while(100), count_repeat(7), list(10, loops()), list(5, jumps()),
  list(9, logic(1, false)), list(9, logic(nil, "y"))
