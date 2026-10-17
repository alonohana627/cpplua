-- nil cannot be a table key (stage 6); reading t[nil] is fine, writing is not.
local t = {}
print(t[nil])
local key
t[key] = 1
