-- The length of a nil field (stage 6).
local t = {items = {1, 2}}
print(#t.items)
return #t.missing
