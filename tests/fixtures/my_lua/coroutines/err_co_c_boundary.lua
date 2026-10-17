-- An uncaught yield across a C call, through coroutine.wrap.
local sorter = coroutine.wrap(function(t)
  table.sort(t, function(a, b) coroutine.yield(a) return a < b end)
  return t
end)
print(sorter({3, 1, 2}))
