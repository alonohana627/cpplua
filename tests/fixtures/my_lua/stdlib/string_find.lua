-- string.find (stage 9): plain searches, patterns with captures and position
-- captures, the init argument (positive, negative, zero, past the end), the
-- plain flag turning magic characters into ordinary ones, anchors, and the
-- empty pattern.

local s = "hello world, hello lua"
print(string.find(s, "hello"))
print(string.find(s, "hello", 2))
print(string.find(s, "hello", -9))
print(string.find(s, "hello", -100))
print(string.find(s, "lua", 100))
print(string.find(s, "xyz"))
print(string.find(s, "o", 0), string.find(s, "o", 6))
print(string.find(s, "a", -1), string.find(s, "a", -2))

-- The empty pattern matches at init, even just past the end.
print(string.find("abc", ""), string.find("abc", "", 3), string.find("abc", "", 4))
print(string.find("abc", "", 5), string.find("", ""), string.find("", "", 1))
print(string.find("abc", "", -1), string.find("abc", "", -10))

-- Patterns.
print(string.find(s, "w%a+"))
print(string.find(s, "(%a+), (%a+)"))
print(string.find(s, "()ll()"))
print(string.find(s, "l+"))
print(string.find("  trim  ", "^%s*(.-)%s*$"))
print(string.find("key = value", "(%w+)%s*=%s*(%w+)"))
print(string.find("2024-01-15", "(%d+)-(%d+)-(%d+)"))
print(string.find("abc", "^b"), string.find("abc", "^a"), string.find("abc", "c$"))
print(string.find("abc", "b", 1, false), string.find("a.c", ".", 1, nil))

-- Plain searches ignore magic characters.
print(string.find("a.c", ".", 1, true), string.find("a+b", "+", 1, true))
print(string.find("50% off", "%", 1, true), string.find("(x)", "(", 1, true))
print(string.find("a.b.c", ".b.", 1, true), string.find("abc", "^a", 1, true))
print(string.find("x[y]", "[y]", 1, true), string.find("x[y]", "[y]"))
print(string.find("a\0b", "\0", 1, true), string.find("a\0b", "%z"))
print(string.find("aaa", "aa", 2, true), string.find("aaa", "aaaa", 1, true))

-- Numbers are converted to strings.
print(string.find(12345, 34), string.find(1.5, ".", 1, true))

-- Method syntax.
print(("find me"):find("me"), ("find me"):find("%s"))

print(pcall(string.find))
print(pcall(string.find, "abc"))
print(pcall(string.find, "abc", "b", "x"))

return string.find("the end", "(e)(n)(d)")
