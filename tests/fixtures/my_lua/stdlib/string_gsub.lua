-- string.gsub (stage 9): string replacements with %0..%9 and %%, table and
-- function replacements (false or nil keeps the original match), the n limit,
-- the returned count, anchored patterns, empty matches, the %f frontier
-- pattern, and the errors for invalid replacements.

print(string.gsub("hello world", "o", "0"))
print(string.gsub("hello world", "o", "0", 1))
print(string.gsub("hello world", "o", "0", 0))
print(string.gsub("hello world", "o", "0", -1))
print(string.gsub("hello world", "(%w+)", "<%1>"))
print(string.gsub("hello world", "%w+", "<%0>"))
print(string.gsub("hello world", "(%w+) (%w+)", "%2 %1"))
print(string.gsub("abc", "", "-"))
print(string.gsub("abc", ".", "%0%0"))
print(string.gsub("100%", "%%", "%%%%"))
print(string.gsub("x", "x", "%%"))
print(string.gsub("hello", "l+", ""))
print(string.gsub("", "x*", "y"))
print(string.gsub("abc", "^a", "A"), string.gsub("aaa", "^a", "A"))
print(string.gsub("abc", "c$", "C"), string.gsub("abc", "b$", "B"))
print(string.gsub("abc", "%w", "%1"))
print(string.gsub("a,b,,c", ",", ";", 2))
print(string.gsub("abc", "b*", "-"))

-- Table replacements look up the first capture (or the whole match).
local vars = {name = "Lua", version = 5.4, missing = false}
print(string.gsub("$name $version $missing $unknown", "%$(%w+)", vars))
print(string.gsub("a b c", "%a", {a = 1, b = true and "B"}))

-- Function replacements get all captures; false or nil keeps the match.
print(string.gsub("1 2 3", "%d", function(d) return d * 2 end))
print(string.gsub("abc", "%a", function(c) if c == "b" then return nil end return c:upper() end))
print(string.gsub("abc", "%a", function() return false end))
print(string.gsub("k1=v1 k2=v2", "(%w+)=(%w+)", function(k, v) return v .. "=" .. k end))
print(string.gsub("abc", "()", function(p) return p end))
print(string.gsub("hello", "l", function(m) return m .. 1.5 end))

-- The frontier pattern %f.
print(string.gsub("THE (quick) fox", "%f[%a]%a+", "W"))
print(string.gsub("hello world", "%f[%w]%w", string.upper))
print(string.gsub("aaa bbb", "%f[%l]", "|"))
print(string.gsub("aaa bbb", "%f[%L]", "|"))
print(string.gsub("THE END", "%f[%z]", "!"))

-- Errors.
print(pcall(string.gsub, "abc", "(a)", "%2"))
print(pcall(string.gsub, "abc", "a", "%x"))
print(pcall(string.gsub, "abc", "a", "%"))
print(pcall(string.gsub, "abc", "a", {a = {}}))
print(pcall(string.gsub, "abc", "a", function() return {} end))
print(pcall(string.gsub, "abc", "a", true))
print(pcall(string.gsub, "abc", "a"))
print(pcall(string.gsub, "abc", "a", "b", "x"))

-- Number replacements are converted to strings.
print(string.gsub("abc", "b", 42), string.gsub("abc", "b", 1.5))

return string.gsub("one two three", "(%w+)", "%1!", 2)
