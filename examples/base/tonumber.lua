-- tonumber(e [, base]): converts to a number, or returns nil.
print(tonumber("42"), tonumber("0x1F"), tonumber("  3.5e2  "))      --> 42	31	350.0
print(tonumber("hello"))                                            --> nil
print(tonumber("ff", 16), tonumber("777", 8), tonumber("zz", 36))   --> 255	511	1295
print(tonumber("102", 2))                     --> nil (2 is not a binary digit)
