-- An uncaught error (stage 9): utf8.char of a negative code point.
print(utf8.char(72, 105))
print(utf8.char(-1))
