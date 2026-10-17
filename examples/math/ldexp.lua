-- math.ldexp(m, e): m * 2^e (deprecated 5.2 function, the inverse of frexp).
print(math.ldexp(0.5, 4))                     --> 8.0
print(math.ldexp(math.frexp(12.5)))           --> 12.5
