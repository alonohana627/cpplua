-- An uncaught error (stage 9): a malformed pattern raises an error with the
-- position of the call.
print(("abc"):find("[a-c]+"))
print(("abc"):find("[a-c"))
