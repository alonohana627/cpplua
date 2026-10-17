-- An uncaught error (stage 9): os.time with a table missing the day field.
print(math.type(os.time({year = 2000, month = 1, day = 1})))
print(os.time({year = 2000, month = 1}))
