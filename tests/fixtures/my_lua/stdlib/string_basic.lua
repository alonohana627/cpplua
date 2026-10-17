-- string.len, byte, char, lower, upper, rep, reverse (stage 9): byte with
-- ranges and negative indices, char with 0 and 255 and its range error, rep
-- with a separator and non-positive counts, embedded zeros everywhere.

print(string.len(""), string.len("abc"), string.len("a\0b"), #"a\0b\0")
print(pcall(string.len))
print(string.len(12), string.len(12.5), string.len(-0.0))

-- byte.
print(string.byte("A"), string.byte("ABC", 2), string.byte("ABC", -1))
print(string.byte("ABC", 1, 3))
print(string.byte("ABC", 1, -1))
print(string.byte("ABC", -2, -1))
print(string.byte("ABC", 0, 2))
print(string.byte("ABC", 3, 10))
print(select("#", string.byte("ABC", 4)), select("#", string.byte("ABC", 2, 1)))
print(select("#", string.byte("")), string.byte("\0\255", 1, 2))
print(pcall(string.byte, "abc", "x"))
print(pcall(string.byte, "abc", 1.5))

-- char.
print(string.char(72, 105), string.char() == "", #string.char(0, 0), string.char(65.0))
print(string.char(string.byte("hello", 1, -1)))
print(pcall(string.char, 256))
print(pcall(string.char, -1))
print(pcall(string.char, 65, "x"))
print(string.char(0x41, "66"), string.byte(string.char(255)))

-- lower and upper only change ASCII letters.
print(string.lower("Hello WORLD 123 _!"), string.upper("Hello world 123 _!"))
print(string.upper("a\0b") == "A\0B", #string.upper("a\0b"), string.lower(""))
print(string.upper(42), string.lower(1.5))

-- rep.
print(string.rep("ab", 3), string.rep("ab", 3, ","), string.rep("x", 1, "SEP"))
print("[" .. string.rep("ab", 0) .. "]", "[" .. string.rep("ab", -5) .. "]")
print("[" .. string.rep("", 100) .. "]", string.rep("-", 5, ""))
print(string.rep("a", 2, "\0") == "a\0a", string.rep(5, 3), string.rep("x", 3.0))
print(pcall(string.rep, "x", 1.5))
print(pcall(string.rep, "x"))
print(#string.rep("abc", 1000, "de"))

-- reverse.
print(string.reverse("abc"), string.reverse(""), string.reverse("a"), string.reverse(123))
print(string.reverse("a\0b") == "b\0a", string.reverse(string.reverse("palindrome")))

return string.char(76, 117, 97), string.byte("Lua", -3, -1)
