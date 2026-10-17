-- An uncaught error (stage 9): math.random with an empty interval.
math.randomseed(1)
print(math.random(5, 5))
print(math.random(2, 1))
