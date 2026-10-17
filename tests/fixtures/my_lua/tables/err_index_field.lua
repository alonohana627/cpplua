-- Indexing a nil field in a chain names the field (stage 6): a.b is nil, so
-- a.b.c fails on field 'b'.
local a = {x = {y = 1}}
print(a.x.y)
return a.b.c
