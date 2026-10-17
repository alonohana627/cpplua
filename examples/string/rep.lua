-- string.rep(s, n [, sep]): n copies of s, optionally separated by sep.
print(string.rep("ab", 3))                    --> ababab
print(string.rep("x", 3, ", "))               --> x, x, x
print(string.rep("x", 0) == "")               --> true
