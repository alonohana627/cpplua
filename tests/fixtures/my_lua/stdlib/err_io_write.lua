-- An uncaught error (stage 9): io.write of a value that is neither a string
-- nor a number (the first argument is written before the error).
io.write("written first\n", {})
