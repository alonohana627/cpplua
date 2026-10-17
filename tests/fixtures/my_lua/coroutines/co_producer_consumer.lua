-- Producer, filters and consumer as coroutines.
local function producer(items)
  return coroutine.create(function()
    for _, x in ipairs(items) do coroutine.yield(x) end
    return nil
  end)
end
local function receive(prod)
  local ok, x = coroutine.resume(prod)
  assert(ok, x)
  return x
end
local function filter(prod, f)
  return coroutine.create(function()
    while true do
      local x = receive(prod)
      if x == nil then return nil end
      local y = f(x)
      if y ~= nil then coroutine.yield(y) end
    end
  end)
end
local function line_numbers(prod)
  local n = 0
  return filter(prod, function(x) n = n + 1; return string.format("%3d %s", n, x) end)
end
local p = producer({"alpha", "beta", "", "gamma", "delta", ""})
local non_empty = filter(p, function(x) if x ~= "" then return x end end)
local upper = filter(non_empty, string.upper)
local numbered = line_numbers(upper)
local out = {}
while true do
  local x = receive(numbered)
  if x == nil then break end
  print(x)
  out[#out + 1] = x
end
print(coroutine.status(numbered), coroutine.status(p))
-- consumer-driven: the consumer is the coroutine, and the producer sends values in
local consumer = coroutine.wrap(function()
  local total = 0
  while true do
    local v = coroutine.yield(total)
    if v == nil then break end
    total = total + v
  end
  return "total", total
end)
consumer()
for i = 1, 10 do consumer(i) end
return #out, consumer(nil)
