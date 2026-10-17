-- math.randomseed([x [, y]]): seeds the generator; the same seed repeats the same sequence.
math.randomseed(123)
local first = { math.random(100), math.random(100), math.random(100) }
math.randomseed(123)
local second = { math.random(100), math.random(100), math.random(100) }
print(table.concat(first, ",") == table.concat(second, ","))   --> true
