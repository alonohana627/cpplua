-- string.format(fmt, ...): printf-style formatting.
print(string.format("%d items at %.2f each", 3, 9.5))    --> 3 items at 9.50 each
print(string.format("[%5s] [%-5s]", "ab", "ab"))         --> [   ab] [ab   ]
print(string.format("%x %X %o", 255, 255, 8))            --> ff FF 10
print(string.format("%q", 'a "quoted"\nline'))           --> "a \"quoted\"\
--> line"
print(string.format("%s and %s", true, nil))             --> true and nil
print(string.format("%5.1f%%", 99.44))                   -->  99.4%
