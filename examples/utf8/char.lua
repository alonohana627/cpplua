-- utf8.char(...): converts code points to a UTF-8 string.
print(utf8.char(72, 233, 108, 108, 246))      --> Héllö
print(utf8.char(0x1F600) == "\u{1F600}")      --> true
