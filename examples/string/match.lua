-- string.match(s, pattern [, init]): the captures of the first match (or the whole match).
print(string.match("2026-10-17", "(%d+)-(%d+)-(%d+)"))   --> 2026	10	17
print(string.match("price: 42 USD", "%d+"))              --> 42
print(string.match("  trim me  ", "^%s*(.-)%s*$"))       --> trim me
print(string.match("hello", "()ll()"))                   --> 3	5 (position captures)
print(string.match("hello", "xyz"))                      --> nil
