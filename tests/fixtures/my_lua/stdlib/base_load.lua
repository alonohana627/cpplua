-- load (stage 9): string chunks with results and varargs, chunk names in
-- error messages ("=name", "@file" and the default [string "..."]), mode "t"
-- and "b", the env parameter (a chunk sees only its own _ENV), syntax errors
-- returned as nil plus a message, and reader functions returning pieces.

-- A string chunk is a vararg function.
local f = load("return 1 + 2")
print(type(f), f())
print(load("local a, b = ... return b, a")(10, 20))
print(load("return select('#', ...)")(nil, nil, nil))
print(load("x_from_load = 5")(), x_from_load)
print(load("")())

-- Syntax errors come back as nil plus a message; nothing is raised.
print(load("return +"))
print(load("x ="))
print(load("for"))
print(load("return 'unfinished"))
print(load("x = }", "=mychunk"))
print(load("\n\nlocal = 1", "@virtual.lua"))
print(load("return ) -- a long first line that is shortened in the chunk name", nil))
print(load("a b\nc", "multi\nline"))

-- Chunk names appear in runtime errors too.
print(pcall(load("error('boom')", "=custom")))
print(pcall(load("local t = nil; return t.x", "@fake.lua")))
print(pcall(load("error('in string')")))
print(pcall(load("return 1 < {}", "=(load)")))

-- Modes: "t" allows text, "b" rejects it.
print(load("return 1", "c", "t")())
print(load("return 1", "c", "b"))
print(load("return 1", "c", "bt")())
print(pcall(load, "return 1", "c", "x"))

-- The env parameter becomes the chunk's _ENV.
local env = {y = 7}
local g = load("y = y * 2; z = 1; return y", "env", "t", env)
print(g(), env.y, env.z, y, z)
print(load("return print", "c", "t", {})())
print(pcall(load("return print('x')", "c", "t", {})))
print(load("return _ENV", "c", "t", env)() == env)
local nil_env = load("return x", "nilenv", "t", nil)
print(pcall(nil_env))

-- A reader function returns pieces until nil or "".
local pieces = {"return ", "1 ", "+ ", "41", nil}
local i = 0
local reader = load(function() i = i + 1 return pieces[i] end)
print(reader())
local parts = {"local a = 'abc'", " return a:upper(), #a", ""}
local j = 0
print(load(function() j = j + 1 return parts[j] end)())
local one_char = "return 'one char at a time'"
local k = 0
print(load(function() k = k + 1 return one_char:sub(k, k) end, "=reader")())
print(type(load(function() return nil end)))
print(pcall(load(function() return nil end)))
print(pcall(load, function() return {} end))
print(pcall(load, function() error("reader failed", 0) end))
local syntax_pieces = {"return ", "1 +"}
local m = 0
print(load(function() m = m + 1 return syntax_pieces[m] end, "=pieces"))

-- Errors in the arguments.
print(pcall(load))
print(pcall(load, 42))
print(pcall(load, {}))

-- Chunks are fresh closures sharing the global environment.
local counter_src = "count_global = (count_global or 0) + 1 return count_global"
local c1, c2 = load(counter_src), load(counter_src)
print(c1(), c2(), c1(), c1 == c2)

return load("return ...", "=v")("a", "b")
