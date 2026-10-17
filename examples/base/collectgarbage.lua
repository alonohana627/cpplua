-- collectgarbage([opt [, ...]]): talks to the garbage collector.
local before = collectgarbage("count")        -- memory in use, in Kbytes
local junk = {}
for i = 1, 10000 do junk[i] = { i } end
junk = nil
collectgarbage()                              -- "collect": a full cycle
local after = collectgarbage("count")
print(type(before), after < before + 1000)    --> number	true

collectgarbage("stop")
print(collectgarbage("isrunning"))            --> false
collectgarbage("restart")
print(collectgarbage("isrunning"))            --> true
