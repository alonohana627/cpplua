-- An uncaught error (stage 9): a missing count for string.rep, called as a
-- method, so the argument number is shifted in the message.
print(("ab"):rep(2))
print(("ab"):rep())
