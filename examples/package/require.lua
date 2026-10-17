-- require(modname): loads a module once and caches it in package.loaded.
package.preload["greeter"] = function(name)
  print("loading " .. name)
  return { hello = function(who) return "hello, " .. who end }
end

local greeter = require("greeter")            --> loading greeter
print(greeter.hello("Lua"))                   --> hello, Lua
print(require("greeter") == greeter)          --> true (no second "loading")

-- Modules found on package.path are files.
local dir = os.tmpname()
os.remove(dir)
local name = dir .. "_mymod"
local f = assert(io.open(name .. ".lua", "w"))
f:write("return { value = 99 }\n")
f:close()
package.path = name .. ".lua;" .. package.path
print(require("anything_at_all_matches").value)   --> 99 (the template has no "?")
os.remove(name .. ".lua")
