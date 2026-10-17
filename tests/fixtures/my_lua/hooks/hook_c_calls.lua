-- Call and return hooks see every call: the iterators of pairs and ipairs
-- (which the VM otherwise runs inline), and a tail call to a C function,
-- whose caller returns through it (issue #12).
local log = {}
local function f() return select('#', 1, 2) end
debug.sethook(function(event)
    local info = debug.getinfo(2, "nS")
    log[#log + 1] = event .. ":" .. (info.name or info.what)
end, "cr")
f()
for _ in pairs({1}) do end
for _ in ipairs({1}) do end
debug.sethook()
print(table.concat(log, " "))
