-- math.ult(m, n): true when m < n comparing them as unsigned integers.
print(math.ult(1, 2))                         --> true
print(math.ult(-1, 1))                        --> false (-1 is the largest unsigned value)
print(1 < -1, math.ult(1, -1))                --> false	true
