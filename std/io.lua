---@meta

-- The io library's functions are implemented in C++ in lib/std/io.cpp.
-- This file is a reference only (for readers and lua-language-server); lua2 never runs it.

---The io library. Functions that fail return nil (fail), an error message and an error code.
---Writes to the standard output and error go through the interpreter's output functions,
---so a program embedding lua2 can capture them.
---@class iolib
io = {}

---A file handle (a userdata). Its methods are called with `:`, as in `f:read("l")`.
---@class file*
local file = {}

---The standard input file.
---@type file*
io.stdin = file

---The standard output file. Its output goes through the interpreter's output function.
---@type file*
io.stdout = file

---The standard error file. Its output goes through the interpreter's error output function.
---@type file*
io.stderr = file

---Closes `file`, or the default output file without an argument.
---@param file? file*
---@return boolean? suc
---@return ("exit"|"signal")? exitcode
---@return integer? code
function io.close(file) end

---Saves any written data to the default output file.
---@return file*?
---@return string? errmsg
function io.flush() end

---With a file name, opens that file in text mode and makes it the default input file;
---with a file handle, makes it the default input. Returns the current default input file.
---@param file? string|file*
---@return file*
function io.input(file) end

---Opens `filename` for reading and returns an iterator that reads it with the given formats
---(default "l"), closing the file at the end of the loop. Without a file name, reads the
---default input and does not close it. Also returns three more values for the generic `for`.
---@param filename? string
---@param ... string|integer
---@return fun(): any, ...
function io.lines(filename, ...) end

---Opens `filename` in `mode` ("r" default, "w", "a", "r+", "w+", "a+", optionally with "b")
---and returns a new file handle, or nil plus an error message.
---@param filename string
---@param mode? "r"|"w"|"a"|"r+"|"w+"|"a+"|"rb"|"wb"|"ab"|"r+b"|"w+b"|"a+b"
---@return file*?
---@return string? errmsg
function io.open(filename, mode) end

---With a file name, opens that file in text mode for writing and makes it the default output;
---with a file handle, makes it the default output. Returns the current default output file.
---@param file? string|file*
---@return file*
function io.output(file) end

---In Lua, starts the program `prog` with a pipe to it. lua2's io library uses only the C++
---standard library, which cannot do that: after checking its arguments it raises
---"'popen' not supported", as a Lua built without popen does.
---@param prog string
---@param mode? "r"|"w"
---@return file*?
---@return string? errmsg
function io.popen(prog, mode) end

---The same as `io.input():read(...)`.
---@param ... "n"|"a"|"l"|"L"|integer
---@return any ...
function io.read(...) end

---Returns a handle for a new temporary file (in the system's temporary folder) opened in
---update mode; it is removed when the file is closed or collected.
---@return file*
function io.tmpfile() end

---Returns "file" for an open file handle, "closed file" for a closed one, and nil otherwise.
---@param obj any
---@return "file"|"closed file"|nil
function io.type(obj) end

---The same as `io.output():write(...)`.
---@param ... string|number
---@return file*?
---@return string? errmsg
function io.write(...) end

---Closes the file. Files are also closed when their handles are collected.
---@return boolean? suc
---@return ("exit"|"signal")? exitcode
---@return integer? code
function file:close() end

---Saves any written data to the file.
---@return file*?
---@return string? errmsg
function file:flush() end

---Returns an iterator that reads the file with the given formats (default "l") on each call.
---Unlike `io.lines`, it does not close the file when the loop ends.
---@param ... "n"|"a"|"l"|"L"|integer
---@return fun(): any, ...
function file:lines(...) end

---Reads the file according to the formats: "n" a numeral, "a" the rest of the file,
---"l" the next line without its newline (default), "L" with it, or a number of bytes.
---Returns a value per format, or nil when it cannot read data in that format.
---@param ... "n"|"a"|"l"|"L"|integer
---@return any ...
function file:read(...) end

---Sets the file position to `offset` bytes from `whence` ("set", "cur" default, or "end")
---and returns the final position from the start of the file.
---@param whence? "set"|"cur"|"end"
---@param offset? integer
---@return integer? position
---@return string? errmsg
function file:seek(whence, offset) end

---Sets the buffering mode of the file: "no", "full" or "line", with an optional buffer size.
---@param mode "no"|"full"|"line"
---@param size? integer
---@return boolean? suc
---@return string? errmsg
function file:setvbuf(mode, size) end

---Writes each argument (a string or number) to the file and returns the file.
---@param ... string|number
---@return file*?
---@return string? errmsg
function file:write(...) end
