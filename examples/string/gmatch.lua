-- string.gmatch(s, pattern [, init]): an iterator over all matches.
for word in string.gmatch("one two three", "%a+") do
  print(word)
end
--> one
--> two
--> three

for k, v in string.gmatch("a=1, b=2", "(%w+)=(%w+)") do
  print(k, v)
end
--> a	1
--> b	2
