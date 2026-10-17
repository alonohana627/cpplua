-- An uncaught error (stage 9): arithmetic on a string that is not a number
-- goes through the string metatable and fails there.
print("10" + 1)
print("ten" + 1)
