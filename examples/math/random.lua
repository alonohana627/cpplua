-- math.random([m [, n]]): a float in [0, 1), or an integer in [1, m] or [m, n].
math.randomseed(42)
local f = math.random()
print(f >= 0 and f < 1)                       --> true
local d = math.random(6)
print(d >= 1 and d <= 6, math.type(d))        --> true	integer
local r = math.random(10, 20)
print(r >= 10 and r <= 20)                    --> true
