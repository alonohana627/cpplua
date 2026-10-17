-- Hooks do not run inside __gc finalizers (GCTM's allowhook; issue #8).
collectgarbage("stop")
local events = 0
local in_gc = false
debug.sethook(function() if in_gc then events = events + 1 end end, "crl")
do
    local _ = setmetatable({}, {__gc = function()
        in_gc = true
        local x = 1
        for i = 1, 3 do x = x + i end
        in_gc = false
    end})
end
collectgarbage()
debug.sethook()
print(events)
