-- package.preload: loader functions for specific modules, tried before the paths.
package.preload["config"] = function()
  return { debug = true }
end
print(require("config").debug)                --> true
