-- utf8.codes(s [, lax]): iterates (byte position, code point) over every character.
for pos, code in utf8.codes("aé€") do
  print(pos, code)
end
--> 1	97
--> 2	233
--> 4	8364
