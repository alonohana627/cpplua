-- io.popen(prog [, mode]): runs a program, connected to its output (or input, with "w").
local ok, p = pcall(io.popen, "echo hello from a child process")
if ok and p then
  print((p:read("a"):gsub("%s+$", "")))       --> hello from a child process
-- (lua2 does not support io.popen yet, so it takes the else branch.)
  p:close()
else
  print("io.popen is not supported here")
end
