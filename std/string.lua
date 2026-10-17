---@meta

-- The string library's functions are implemented in C++ in lib/std/string.cpp.
-- This file is a reference only (for readers and lua-language-server); lua2 never runs it.

---The string library. Strings have it as their metatable's `__index`,
---so `s:upper()` means `string.upper(s)`. Indices may be negative (counting from the end).
---@class stringlib
string = {}

---Returns the internal numeric codes of the characters `s[i]` to `s[j]`.
---`i` defaults to 1 and `j` to `i`.
---@param s string|number
---@param i? integer
---@param j? integer
---@return integer ...
function string.byte(s, i, j) end

---Returns a string whose characters have the given internal numeric codes (0 to 255).
---@param ... integer
---@return string
function string.char(...) end

---Would return a binary chunk of function `f`. lua2 has no binary chunks, so it always
---raises the error "unable to dump given function".
---@param f function
---@param strip? boolean
---@return string
function string.dump(f, strip) end

---Looks for the first match of `pattern` in `s` from position `init` (default 1).
---Returns the start and end indices plus any captures, or nil. With `plain` true,
---`pattern` is a plain substring.
---@param s string|number
---@param pattern string|number
---@param init? integer
---@param plain? boolean
---@return integer? start
---@return integer? end
---@return any ... captures
function string.find(s, pattern, init, plain) end

---Returns a formatted version of its arguments, following the printf-style `formatstring`.
---Supports %d %i %u %c %x %X %o %e %E %f %F %g %G %a %A %s %q %p and %%.
---@param formatstring string|number
---@param ... any
---@return string
function string.format(formatstring, ...) end

---Returns an iterator that, on each call, returns the next captures of `pattern` in `s`
---(or the whole match when there are none), starting at `init` (default 1).
---@param s string|number
---@param pattern string|number
---@param init? integer
---@return fun(): string|integer, ...
function string.gmatch(s, pattern, init) end

---Returns a copy of `s` with all (or the first `n`) matches of `pattern` replaced by `repl`
---(a string with %0-%9, a table indexed by the first capture, or a function of the captures),
---plus the number of matches.
---@param s string|number
---@param pattern string|number
---@param repl string|number|table|function
---@param n? integer
---@return string
---@return integer count
function string.gsub(s, pattern, repl, n) end

---Returns the length of `s` in bytes.
---@param s string|number
---@return integer
function string.len(s) end

---Returns a copy of `s` with uppercase letters changed to lowercase.
---@param s string|number
---@return string
function string.lower(s) end

---Looks for the first match of `pattern` in `s` from `init` (default 1) and returns
---its captures (or the whole match), or nil.
---@param s string|number
---@param pattern string|number
---@param init? integer
---@return any ...
function string.match(s, pattern, init) end

---Returns `n` copies of `s` separated by `sep` (default empty); "" when `n` <= 0.
---@param s string|number
---@param n integer
---@param sep? string|number
---@return string
function string.rep(s, n, sep) end

---Returns `s` reversed.
---@param s string|number
---@return string
function string.reverse(s) end

---Returns the substring of `s` from `i` to `j` (default -1, the end).
---@param s string|number
---@param i? integer
---@param j? integer
---@return string
function string.sub(s, i, j) end

---Returns a copy of `s` with lowercase letters changed to uppercase.
---@param s string|number
---@return string
function string.upper(s) end

---Returns a binary string with the values packed according to the format string `fmt`
---(options such as < > = ! b B h H i[n] I[n] l L j J T f d n s[n] z x X).
---@param fmt string
---@param ... any
---@return string
function string.pack(fmt, ...) end

---Returns the size of a string produced by `string.pack` with format `fmt`.
---The format cannot have the variable-length options "s" or "z".
---@param fmt string
---@return integer
function string.packsize(fmt) end

---Returns the values packed in `s` according to `fmt`, starting at `pos` (default 1),
---followed by the index of the first unread byte.
---@param fmt string
---@param s string
---@param pos? integer
---@return any ...
function string.unpack(fmt, s, pos) end
