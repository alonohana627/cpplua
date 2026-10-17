-- math.fmod(x, y): the remainder of x / y, rounding the quotient towards zero.
print(math.fmod(7, 3), math.fmod(-7, 3))      --> 1	-1
print(-7 % 3)                                 --> 2 (% rounds towards minus infinity instead)
print(math.fmod(5.5, 2))                      --> 1.5
