---@meta

-- The os library's functions are implemented in C++ in lib/std/os.cpp.
-- This file is a reference only (for readers and lua-language-server); cpplua never runs it.

---The os library: access to the operating system.
---@class oslib
os = {}

---The table `os.date("*t")` returns.
---@class osdate
---@field year integer
---@field month integer 1-12
---@field day integer 1-31
---@field hour integer 0-23
---@field min integer 0-59
---@field sec integer 0-61
---@field wday integer weekday, 1-7, Sunday is 1
---@field yday integer day of the year, 1-366
---@field isdst boolean daylight saving flag

---The table `os.time` takes.
---@class osdateparam
---@field year integer
---@field month integer
---@field day integer
---@field hour? integer default 12
---@field min? integer default 0
---@field sec? integer default 0
---@field isdst? boolean

---Returns an approximation of the CPU time used by the program, in seconds.
---@return number
function os.clock() end

---Returns a string or a table with the date and time `time` (default now), formatted by
---`format` as for C's strftime. A leading "!" means UTC; "*t" returns a table with fields
---year, month, day, hour, min, sec, wday, yday and isdst.
---@param format? string
---@param time? integer
---@return string|osdate
function os.date(format, time) end

---Returns the difference in seconds from time `t1` to time `t2` (that is, `t2 - t1`).
---@param t2 integer
---@param t1 integer
---@return number
function os.difftime(t2, t1) end

---Runs `command` in an operating-system shell. Returns true (or nil on failure), then "exit"
---or "signal", then the exit status or signal number. Without a command, returns whether a
---shell is available.
---@param command? string
---@return boolean? suc
---@return ("exit"|"signal")? exitcode
---@return integer? code
function os.execute(command) end

---Ends the program with `code`: true (default) means success, false failure, or a number.
---With `close` true, closes the Lua state before exiting.
---@param code? boolean|integer
---@param close? boolean
function os.exit(code, close) end

---Returns the value of the environment variable `varname`, or nil if it is not defined.
---@param varname string
---@return string?
function os.getenv(varname) end

---Deletes the file (or empty directory) `filename`. Returns true, or nil plus an error message.
---@param filename string
---@return boolean? suc
---@return string? errmsg
function os.remove(filename) end

---Renames the file or directory `oldname` to `newname`. Returns true, or nil plus a message.
---@param oldname string
---@param newname string
---@return boolean? suc
---@return string? errmsg
function os.rename(oldname, newname) end

---Sets the program's current locale (nil only queries it) for `category`: "all" (default),
---"collate", "ctype", "monetary", "numeric" or "time". Returns the locale name, or nil.
---@param locale? string
---@param category? "all"|"collate"|"ctype"|"monetary"|"numeric"|"time"
---@return string?
function os.setlocale(locale, category) end

---Returns the current time, or the time given by table `date` (fields year, month, day, and
---optionally hour (default 12), min, sec, isdst). Normalizes the fields of `date`.
---@param date? osdateparam
---@return integer
function os.time(date) end

---Returns the name of a new empty file in the system's temporary folder, which keeps the
---name taken (as POSIX Lua's `mkstemp` does). Remove it when done.
---@return string
function os.tmpname() end
