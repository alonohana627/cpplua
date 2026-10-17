-- math.abs(x): the absolute value of x, keeping its integer/float subtype.
print(math.abs(-7), math.abs(-7.5), math.abs(3))   --> 7	7.5	3
print(math.abs(math.mininteger) == math.mininteger) --> true (wraps around)
