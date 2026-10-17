-- An uncaught error (stage 9): select with index 0.
local function pick(n, ...) return select(n, ...) end
print(pick(2, "a", "b"))
print(pick(0, "a", "b"))
