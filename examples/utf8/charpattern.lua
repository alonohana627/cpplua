-- utf8.charpattern: a pattern matching exactly one UTF-8 byte sequence.
local chars = {}
for ch in string.gmatch("añb€", utf8.charpattern) do
  chars[#chars + 1] = "[" .. ch .. "]"
end
print(table.concat(chars))                    --> [a][ñ][b][€]
