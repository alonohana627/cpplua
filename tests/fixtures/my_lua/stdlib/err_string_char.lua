-- An uncaught error (stage 9): string.char of a value above 255.
print(string.char(104, 105))
print(string.char(104, 300))
