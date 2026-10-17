-- string.find(s, pattern [, init [, plain]]): start and end of the first match, plus captures.
print(string.find("hello world", "world"))    --> 7	11
print(string.find("hello world", "o", 6))     --> 8	8
print(string.find("key=value", "(%w+)=(%w+)"))   --> 1	9	key	value
print(string.find("1+1=2", "+", 1, true))     --> 2	2 (plain: no magic characters)
print(string.find("abc", "x"))                --> nil
