-- assert(v [, message]): returns all its arguments when v is truthy, raises otherwise.
local a, b = assert(42, "unused message")
print(a, b)                                   --> 42	unused message

local ok, err = pcall(assert, false, "value must not be false")
print(ok, err)                                --> false	value must not be false

ok, err = pcall(assert, nil)
print(ok, err)                                --> false	assertion failed!
