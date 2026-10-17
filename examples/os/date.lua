-- os.date([format [, time]]): formats a time; "*t" / "!*t" return a table instead.
local t = 0                                   -- the epoch
print(os.date("!%Y-%m-%d %H:%M:%S", t))       --> 1970-01-01 00:00:00
local parts = os.date("!*t", 86400 * 31)
print(parts.year, parts.month, parts.day, parts.wday)   --> 1970	2	1	1
print(type(os.date()))                        --> string (now, in the default format)
