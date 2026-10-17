-- os.time, os.date, os.difftime, os.clock, os.getenv (stage 9), only the
-- deterministic parts: os.time with a table (local time, so only differences
-- between two calls are printed), field normalization (month 13, day 0,
-- negative hours), os.date with "!" (UTC) formats for fixed times, "!*t"
-- tables, difftime, the types of clock and time results, a variable that
-- does not exist, and the errors for bad tables and formats.

local base = os.time({year = 2020, month = 1, day = 1, hour = 12})
print(math.type(base), os.time({year = 2020, month = 1, day = 2, hour = 12}) - base)
print(os.time({year = 2020, month = 3, day = 1, hour = 12}) - base)   -- 2020 is a leap year
print(os.time({year = 2021, month = 1, day = 1, hour = 12}) - base)
print(os.time({year = 2020, month = 1, day = 1, hour = 13, min = 30, sec = 15}) - base)
print(os.time({year = 2020, month = 13, day = 1, hour = 12}) - os.time({year = 2021, month = 1, day = 1, hour = 12}))
print(os.time({year = 2020, month = 1, day = 0, hour = 12}) - base)
print(os.time({year = 2020, month = 1, day = 1, hour = -12}) - base)
print(os.time({year = 2020, month = 1, day = 1}) - base)   -- hour defaults to 12
print(os.time({year = "2020", month = "1", day = "1", hour = "12"}) - base)

-- The table is normalized in place.
local t = {year = 2020, month = 2, day = 30, hour = 12}
os.time(t)
print(t.year, t.month, t.day, t.hour, t.min, t.sec, t.wday, t.yday, type(t.isdst))

print(pcall(os.time, {year = 2020}))
print(pcall(os.time, {year = 2020, month = 1}))
print(pcall(os.time, {year = 2020, month = 1.5, day = 1}))
print(pcall(os.time, {year = 2020, month = "x", day = 1}))
print(pcall(os.time, 5))

-- os.date in UTC for fixed times.
print(os.date("!%Y-%m-%d %H:%M:%S", 0))
print(os.date("!%Y-%m-%d %H:%M:%S", 86400 * 365 + 3661))
print(os.date("!%Y-%m-%d %H:%M:%S", 1234567890))
print(os.date("!%A %a %B %b %d %j %p %y %%", 1234567890))
print(os.date("!%I %M %S %w %H", 1234567890 + 43200))
print(os.date("!%x %X", 0), os.date("!%m/%d/%y %U %W", 951782400))
print(os.date("!no specifiers", 0), os.date("!", 0) == "")
local d = os.date("!*t", 86400)
print(d.year, d.month, d.day, d.hour, d.min, d.sec, d.wday, d.yday, d.isdst)
d = os.date("!*t", 951782400)   -- 2000-02-29
print(d.year, d.month, d.day, d.wday, d.yday)
print(type(os.date()), type(os.date("*t")), type(os.date("%Y")), math.type(os.date("*t").year))
print(pcall(os.date, "%Ez", 0))
print(pcall(os.date, "%", 0))
print(pcall(os.date, "%Q", 0))
print(pcall(os.date, "!%Y", 1.5))
print(pcall(os.date, "!%Y", "x"))

-- difftime.
print(os.difftime(10, 4), os.difftime(4, 10), os.difftime(5, 5), math.type(os.difftime(1, 1)))
print(pcall(os.difftime, 5))
print(pcall(os.difftime))

-- clock, time, getenv.
print(math.type(os.clock()), os.clock() >= 0, math.type(os.time()))
print(os.getenv("CPPLUA_SURELY_UNDEFINED_VARIABLE_12345"), pcall(os.getenv))

return os.date("!%Y", 0), os.time({year = 2000, month = 1, day = 2, hour = 0}) - os.time({year = 2000, month = 1, day = 1, hour = 0})
