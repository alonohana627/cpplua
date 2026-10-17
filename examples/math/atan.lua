-- math.atan(y [, x]): the arc tangent of y/x, using the signs of both to find the quadrant.
print(math.atan(1) == math.pi / 4)            --> true
print(math.atan(1, -1) == 3 * math.pi / 4)    --> true
print(math.atan(0, -1) == math.pi)            --> true
