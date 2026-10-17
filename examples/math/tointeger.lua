-- math.tointeger(x): x as an integer if it has an exact integer value, otherwise fail (nil).
print(math.tointeger(3.0), math.tointeger(3))     --> 3	3
print(math.tointeger(3.5))                        --> nil
print(math.tointeger("8"))                        --> nil (strings are not converted)
