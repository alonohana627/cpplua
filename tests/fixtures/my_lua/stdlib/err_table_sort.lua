-- An uncaught error (stage 9): table.sort with values that cannot be
-- compared with each other.
local t = {false, true}
table.sort(t)
