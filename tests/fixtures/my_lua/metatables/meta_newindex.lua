-- __newindex (stage 7): called only when assigning to an absent key, as a
-- table (the assignment goes there instead) or as a function receiving the
-- table, key and value; rawset bypasses it. Read-only tables and proxies.

-- __newindex as a table: new keys go to the other table.
local store = {}
local front = setmetatable({existing = 1}, {__newindex = store})
front.existing = 2   -- present: plain assignment
front.fresh = 3      -- absent: goes to store
front[1] = "one"
print(front.existing, front.fresh, store.fresh, rawget(front, "fresh"), store[1], front[1])

-- __newindex as a function.
local log = {}
local tracked = setmetatable({}, {
  __newindex = function(t, k, v)
    log[#log + 1] = tostring(k) .. "=" .. tostring(v)
    rawset(t, k, v)
  end,
})
tracked.a = 1
tracked.a = 2        -- now present: no call
tracked[10] = "ten"
tracked.b = nil      -- absent key, nil value: still called
print(#log, log[1], log[2], log[3], tracked.a, tracked[10])

-- A read-only table through a proxy.
local function readonly(t)
  return setmetatable({}, {
    __index = t,
    __newindex = function(_, k) error("attempt to update a read-only table (key " .. tostring(k) .. ")", 2) end,
    __len = function() return #t end,
  })
end
local days = readonly({"mon", "tue", "wed"})
print(days[1], days[3], #days)
local ok, msg = pcall(function() days[4] = "thu" end)
print(ok, msg)
print(rawget(days, 4), days[4])

-- rawset bypasses __newindex and returns the table.
local guarded = setmetatable({}, {__newindex = function() error("no!") end})
print(rawset(guarded, "k", "v") == guarded, guarded.k)
guarded.k = "changed"  -- present now: no call
print(guarded.k)

-- A chain: __newindex table that itself has a __newindex.
local final = {}
local mid = setmetatable({}, {__newindex = final})
local outer = setmetatable({}, {__newindex = mid})
outer.x = "deep"
print(rawget(outer, "x"), rawget(mid, "x"), final.x)

-- A default-value table counting its writes.
local writes = 0
local counted = setmetatable({}, {
  __index = function() return 0 end,
  __newindex = function(t, k, v) writes = writes + 1 rawset(t, k, v) end,
})
for _, w in ipairs({"a", "b", "a", "c", "a"}) do counted[w] = counted[w] + 1 end
print(counted.a, counted.b, counted.c, counted.d, writes)

-- __newindex is used for integer keys beyond the sequence too.
local ints = setmetatable({1, 2, 3}, {__newindex = function(t, k, v) rawset(t, k, v * 100) end})
ints[2] = 20
ints[4] = 4
print(ints[2], ints[4])

-- Errors in rawset arguments.
print(pcall(rawset, {}, nil, 1))
print(pcall(rawset, {}, 0 / 0, 1))

return store.fresh, final.x, #log
