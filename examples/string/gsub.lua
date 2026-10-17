-- string.gsub(s, pattern, repl [, n]): replaces matches; returns the string and the count.
print(string.gsub("hello world", "o", "0"))               --> hell0 w0rld	2
print(string.gsub("hello world", "o", "0", 1))            --> hell0 world	1
print(string.gsub("hello world", "(%w+)", "<%1>"))        --> <hello> <world>	2
print(string.gsub("$name is $age", "%$(%w+)", { name = "Ann", age = 30 }))  --> Ann is 30	2
print(string.gsub("abc", "%w", function(c) return c:upper() .. "." end))   --> A.B.C.	3
