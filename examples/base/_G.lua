-- _G: the global environment table (a variable, not a function).
answer = 42
print(_G.answer, _G["answer"])                --> 42	42
_G.made_through_G = "yes"
print(made_through_G)                         --> yes
print(_G._G == _G)                            --> true
