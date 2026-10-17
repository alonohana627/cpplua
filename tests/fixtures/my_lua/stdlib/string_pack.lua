-- string.pack, string.unpack, string.packsize (stage 9): little and big
-- endian integers of fixed sizes (b B h H i4 I4 j J and i1..i16), floats and
-- doubles (f d n), strings with length prefixes (s1 s2 s4), zero-terminated
-- strings (z), fixed strings (c5), padding (x), alignment with "!" and "Xop",
-- the position returned by unpack, and the errors for overflows and bad
-- formats. Bytes are shown in hex so the output stays printable.

local function hex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

print(hex(string.pack("<i4", 1)), hex(string.pack(">i4", 1)), hex(string.pack("<i4", -2)))
print(hex(string.pack("<b", -1)), hex(string.pack("<B", 255)), hex(string.pack("<h", -2)))
print(hex(string.pack(">H", 0xABCD)), hex(string.pack("<I3", 0x123456)), hex(string.pack(">i3", -1)))
print(hex(string.pack("<j", 1)), hex(string.pack(">J", math.mininteger)))
print(hex(string.pack("<i16", -1)), hex(string.pack(">i16", 1)))
print(hex(string.pack("<i1i2i4", 1, 2, 3)), hex(string.pack(">bBhH", 1, 2, 3, 4)))

-- Unpack returns the values and the next position.
print(string.unpack("<i4", string.pack("<i4", 123456)))
print(string.unpack("<b", "\255"), string.unpack("<B", "\255"))
print(string.unpack("<h", "\254\255"), string.unpack(">H", "\1\2"))
print(string.unpack("<i3", "\255\255\127"), string.unpack("<i3", "\0\0\128"))
print(string.unpack("<i16", string.pack("<i16", -5)))
print(string.unpack("<j", string.pack("<j", math.maxinteger)) == math.maxinteger)
print(string.unpack("<i2i2", "\1\0\2\0"))
print(string.unpack("<i2", "\0\0\1\0", 3))
print(string.unpack("<i2", "\0\0\1\0", -2))
print(string.unpack("<I8", "\255\255\255\255\255\255\255\255"))

-- Floats.
print(hex(string.pack("<d", 1.5)), hex(string.pack(">f", -2.0)), hex(string.pack("<n", 0.5)))
print(string.unpack("<d", string.pack("<d", 0.1)), string.unpack("<f", string.pack("<f", 0.25)))
print(string.unpack("<f", string.pack("<f", 0.1)) == 0.1, string.unpack("<d", string.pack("<d", math.huge)))
print(math.type(string.unpack("<d", string.pack("<d", 3))), string.unpack("<n", string.pack("<n", -0.0)))

-- Strings.
print(hex(string.pack("<s1", "abc")), hex(string.pack(">s2", "abc")), hex(string.pack("<s4", "")))
print(hex(string.pack("z", "hi")), hex(string.pack("c5", "ab")), hex(string.pack("c2", "ab")))
print(string.unpack("<s1", "\3abcdef"))
print(string.unpack("z", "hello\0world\0"))
print(string.unpack("zz", "hello\0world\0"))
print(string.unpack("c3", "abcdef"))
print(string.unpack("<s1i2", string.pack("<s1i2", "xy", 7)))
print(string.unpack("c0", "abc") == "", string.unpack("c0", "abc", 4) == "")

-- Padding and alignment.
print(hex(string.pack("<bxb", 1, 2)), hex(string.pack("<bxxi2", 1, 2)))
print(hex(string.pack("<!4 b i4", 1, 2)), hex(string.pack("<!2 b i4", 1, 2)), hex(string.pack("<! b i8", 1, 2)))
print(hex(string.pack("<!8 b Xi8", 1)), hex(string.pack("<!4 b Xh b", 1, 2)))
print(string.unpack("<!4 b i4", string.pack("<!4 b i4", -1, 99)))

-- packsize.
print(string.packsize("<i4"), string.packsize("bhi4j"), string.packsize("<!8 b i8"))
print(string.packsize("c10"), string.packsize("i16"), string.packsize("d"), string.packsize("f"))
print(string.packsize("x"), string.packsize(""), string.packsize("<!4 b Xi4"), string.packsize(" < > = "))

-- Errors.
print(pcall(string.pack, "<b", 128))
print(pcall(string.pack, "<B", -1))
print(pcall(string.pack, "<i2", 40000))
print(pcall(string.pack, "<i17", 1))
print(pcall(string.pack, "<i0", 1))
print(pcall(string.pack, "<s1", string.rep("x", 256)))
print(pcall(string.pack, "z", "a\0b"))
print(pcall(string.pack, "c2", "abc"))
print(pcall(string.pack, "<i4"))
print(pcall(string.pack, "<i4", "x"))
print(pcall(string.pack, "y", 1))
print(pcall(string.pack, "<!3 i4", 1))
print(pcall(string.packsize, "s"))
print(pcall(string.packsize, "z"))
print(pcall(string.unpack, "<i4", "abc"))
print(pcall(string.unpack, "z", "abc"))
print(pcall(string.unpack, "<s1", "\5ab"))
print(pcall(string.unpack, "<i4", "abcd", 10))
print(pcall(string.unpack, "<i9", "\0\0\0\0\0\0\0\0\1"))
print(pcall(string.pack, "c", "a"))
print(pcall(string.pack, "X", 1))

return string.unpack("<i2i2i2", string.pack("<i2i2i2", 1, -1, 300))
