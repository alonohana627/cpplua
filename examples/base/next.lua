-- next(table [, index]): returns the key after index and its value.
local t = { answer = 42 }
print(next(t))                                --> answer	42
print(next(t, "answer"))                      --> nil
print(next({}) == nil)                        --> true (a quick emptiness test)
