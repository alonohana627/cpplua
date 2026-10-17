---@meta

-- The package library's functions are implemented in C++ in lib/std/package.cpp.
-- This file is a reference only (for readers and lua-language-server); lua2 never runs it.

---The package library: loading modules with `require`. lua2 is built without dynamic
---libraries; C++ code adds functions through lua2's C++ API instead.
---@class packagelib
package = {}

---The path `require` uses to search for a Lua loader. Starts from the environment variable
---LUA_PATH_5_4 or LUA_PATH (";;" stands for the default path), or the default path.
---@type string
package.path = ""

---The path `require` would use to search for a C loader. Starts from LUA_CPATH_5_4 or
---LUA_CPATH, or the default. lua2 cannot load what it finds there.
---@type string
package.cpath = ""

---Compile-time configuration, one item per line: the directory separator ("\" on Windows,
---"/" elsewhere), the path separator ";", the substitution mark "?", the executable-directory
---mark "!", and the mark "-" that ends the part of a name ignored when building a C function name.
---@type string
package.config = "\\\n;\n?\n!\n-\n"

---The table `require` uses to control which modules are already loaded: `package.loaded[name]`
---is the value the module returned (or true). Setting an entry to nil makes `require` load it again.
---@type table<string, any>
package.loaded = {}

---A table of loader functions for specific modules, consulted first by `require`.
---@type table<string, function>
package.preload = {}

---The searchers `require` tries in order: the preload searcher, the Lua searcher (package.path),
---the C searcher and the all-in-one searcher (package.cpath). In lua2 a C searcher that finds
---a file raises an error saying that dynamic libraries are not enabled.
---@type function[]
package.searchers = {}

---Would dynamically link the C library `libname` and return its function `funcname`.
---lua2 has no dynamic libraries: it always returns nil, the message
---"dynamic libraries not enabled; check your Lua installation" and "absent".
---@param libname string
---@param funcname string
---@return function?
---@return string? errmsg
---@return ("open"|"init"|"absent")? where
function package.loadlib(libname, funcname) end

---Searches for `name` in `path`: replaces each `sep` (default ".") in the name with `rep`
---(default the directory separator), then tries each "?"-template. Returns the first readable
---file name, or nil plus a message listing every file tried.
---@param name string
---@param path string
---@param sep? string
---@param rep? string
---@return string? filename
---@return string? errmsg
function package.searchpath(name, path, sep, rep) end

---Loads the module `modname`. Returns `package.loaded[modname]` if it is there; otherwise tries
---each of `package.searchers`, calls the loader found with `modname` and its extra value, stores
---the result in `package.loaded[modname]`, and returns it plus the loader data (such as the file name).
---@param modname string
---@return any
---@return any loaderdata
function require(modname) end
