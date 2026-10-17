-- utf8.codepoint(s [, i [, j [, lax]]]): the code points of characters starting in s[i..j].
print(utf8.codepoint("héllo", 1, -1))         --> 104	233	108	108	111
print(utf8.codepoint("€"))                    --> 8364
print(pcall(utf8.codepoint, "\xff"))          --> false	...invalid UTF-8 code
