-- NaN cannot be a table key (stage 6); reading t[0/0] is fine, writing is not.
local t = {}
print(t[0 / 0])
t[0 / 0] = 1
