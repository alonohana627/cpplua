-- os.time([table]): the current time, or the time a date table describes, as an integer.
print(math.type(os.time()))                   --> integer
local t = os.time({ year = 2000, month = 1, day = 1, hour = 12 })
print(os.date("%Y-%m-%d %H:%M", t))           --> 2000-01-01 12:00

-- Fields are normalised: day 32 of January is February 1st.
print(os.date("%m-%d", os.time({ year = 2026, month = 1, day = 32, hour = 12 })))   --> 02-01
