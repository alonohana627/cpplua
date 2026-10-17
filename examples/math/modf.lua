-- math.modf(x): the integral and fractional parts of x (a float integral part prints without ".0").
print(math.modf(3.75))                        --> 3	0.75
print(math.modf(-3.75))                       --> -3	-0.75
print(math.modf(5))                           --> 5	0.0
