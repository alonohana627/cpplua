-- An uncaught error (stage 9): string.pack of a value that does not fit in
-- one signed byte.
print(#string.pack("b", 127))
print(string.pack("b", 128))
