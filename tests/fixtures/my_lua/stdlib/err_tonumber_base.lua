-- An uncaught error (stage 9): tonumber with a base outside 2..36.
print(tonumber("z", 36))
print(tonumber("10", 99))
