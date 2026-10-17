-- os.difftime(t2, t1): the number of seconds from t1 to t2, as a float.
local t1 = os.time({ year = 2026, month = 1, day = 1, hour = 0 })
local t2 = os.time({ year = 2026, month = 1, day = 2, hour = 0 })
print(os.difftime(t2, t1))                    --> 86400.0
