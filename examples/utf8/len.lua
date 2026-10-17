-- utf8.len(s [, i [, j [, lax]]]): the number of characters, or fail plus the bad byte position.
print(utf8.len("héllo"), #"héllo")            --> 5	6
print(utf8.len("abc\xffdef"))                 --> nil	4
