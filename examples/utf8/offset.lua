-- utf8.offset(s, n [, i]): the byte position where the n-th character starts.
local s = "aé€z"
print(utf8.offset(s, 3))                      --> 4 (the € starts at byte 4)
print(utf8.offset(s, -1))                     --> 7 (the last character)
print(s:sub(utf8.offset(s, 2), utf8.offset(s, 3) - 1))   --> é
