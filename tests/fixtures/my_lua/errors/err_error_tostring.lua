-- An uncaught error with a table that has __tostring (stage 8): the
-- standalone interpreter shows the result of __tostring.
local E = {__tostring = function(e) return "MyError: " .. e.msg end}
error(setmetatable({msg = "something went wrong"}, E))
