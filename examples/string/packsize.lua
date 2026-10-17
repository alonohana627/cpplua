-- string.packsize(fmt): the size of a string packed with a fixed-size format.
print(string.packsize("i4 i8"))               --> 12 (no alignment unless "!" asks for it)
print(string.packsize("<i4 i8"))              --> 12
print(string.packsize("!8 i4 i8"))            --> 16 (4 bytes padding before the i8)
print(pcall(string.packsize, "s"))            --> false	...variable-length format...
