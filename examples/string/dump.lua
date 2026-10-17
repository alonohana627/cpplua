-- string.dump(f [, strip]): a binary chunk of f that load can bring back.
local function square(x) return x * x end
local ok, chunk = pcall(string.dump, square)
if ok then
  print(type(chunk), load(chunk, "square", "b")(7))   --> string	49
-- (lua2 cannot dump functions yet, so it takes the else branch.)
else
  print("string.dump is not available: " .. chunk)
end
