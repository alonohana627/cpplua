-- string.len(s): the length of s in bytes (same as #s).
print(string.len("hello"), #"hello")          --> 5	5
print(string.len(""), string.len("a\0b"))     --> 0	3
print(string.len("héllo"))                    --> 6 (é is two bytes in UTF-8)
