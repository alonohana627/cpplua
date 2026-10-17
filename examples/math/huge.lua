-- math.huge: a float greater than any other number (positive infinity).
print(math.huge, -math.huge)                  --> inf	-inf
print(math.huge > math.maxinteger)            --> true
print(1 / 0 == math.huge)                     --> true
