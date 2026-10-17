-- pcall(f, ...): calls f in protected mode; the first result is the status.
print(pcall(function(a, b) return a + b end, 1, 2))   --> true	3
print(pcall(function() error("boom", 0) end))         --> false	boom

local ok, err = pcall(function() local x return x + 1 end)
print(ok, err:match("attempt to perform arithmetic") ~= nil)  --> false	true
