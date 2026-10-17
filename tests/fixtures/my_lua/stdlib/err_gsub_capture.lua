-- An uncaught error (stage 9): a replacement string referring to a capture
-- the pattern does not have.
print(("hello world"):gsub("(%w+)", "<%1>"))
print(("hello world"):gsub("(%w+)", "<%2>"))
