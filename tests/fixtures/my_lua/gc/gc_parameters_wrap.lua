-- The collector's parameters wrap as Lua's bytes do, so no value a program
-- passes makes an undefined shift or overflow (issue #8).
collectgarbage("incremental") -- (the tests run this in both modes)
print(collectgarbage("incremental", 0, 0, -1))
print(collectgarbage("incremental", 1000, 1000, 300))
print(collectgarbage("setpause", 200), collectgarbage("setpause", 200))
print(collectgarbage("setstepmul", 100), collectgarbage("setstepmul", 100))
print(type(collectgarbage("step", math.maxinteger)), type(collectgarbage("step", math.mininteger)))
print(collectgarbage("incremental", 200, 100, 13))
print(collectgarbage("generational", -1, -1), collectgarbage("incremental"))
