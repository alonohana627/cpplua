-- io.stdin: the standard input file handle (the default input file).
print(io.type(io.stdin), io.input() == io.stdin)   --> file	true
-- Try it with: echo hi | lua2 examples/io/stdin.lua
-- local line = io.stdin:read("l")
