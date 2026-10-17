-- An uncaught error (stage 9): calling a method the string table does not
-- have names the method in the message.
local s = "x"
print(s:rep(3))
s:bad()
