-- Indexing a nil upvalue names the upvalue (stage 6).
local settings
local function get() return settings.size end
return get()
