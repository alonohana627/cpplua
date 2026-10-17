---@meta

-- The utf8 library's functions are implemented in C++ in lib/std/utf8.cpp.
-- This file is a reference only (for readers and lua-language-server); cpplua never runs it.

---The utf8 library: basic support for UTF-8. Functions that take a `lax` flag accept
---code points up to 2^31 - 1 and surrogates when it is true; otherwise only valid UTF-8.
---@class utf8lib
utf8 = {}

---The pattern that matches exactly one UTF-8 byte sequence, assuming the subject is valid UTF-8.
---@type string
utf8.charpattern = "[\0-\x7F\xC2-\xFD][\x80-\xBF]*"

---Returns the byte position where the `n`-th character (counting from position `i`) starts.
---`i` defaults to 1, or to `#s + 1` when `n` is negative; `n` = 0 finds the start of the
---character containing byte `i`. Returns nil if there is no such character.
---@param s string
---@param n integer
---@param i? integer
---@return integer?
function utf8.offset(s, n, i) end

---Returns the code points of all characters in `s` that start between byte positions `i`
---(default 1) and `j` (default `i`). Raises an error on an invalid byte sequence.
---@param s string
---@param i? integer
---@param j? integer
---@param lax? boolean
---@return integer ...
function utf8.codepoint(s, i, j, lax) end

---Returns a string with the UTF-8 encodings of the given code points, concatenated.
---@param ... integer
---@return string
function utf8.char(...) end

---Returns the number of UTF-8 characters in `s` that start between positions `i` (default 1)
---and `j` (default -1). On an invalid byte sequence returns nil plus the first invalid position.
---@param s string
---@param i? integer
---@param j? integer
---@param lax? boolean
---@return integer?
---@return integer? errpos
function utf8.len(s, i, j, lax) end

---Returns an iterator so that `for p, c in utf8.codes(s)` visits every character of `s`,
---giving its byte position `p` and code point `c`. Raises an error on invalid bytes.
---@param s string
---@param lax? boolean
---@return fun(s: string, i: integer): integer, integer
---@return string
---@return integer
function utf8.codes(s, lax) end
