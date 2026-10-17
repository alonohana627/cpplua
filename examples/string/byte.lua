-- string.byte(s [, i [, j]]): the numeric codes of s[i..j].
print(string.byte("A"))                       --> 65
print(string.byte("hello", 1, 3))             --> 104	101	108
print(("hello"):byte(-1))                     --> 111 (negative indices count from the end)
