-- An uncaught error("boom") at level 1 stops the program with the position
-- of the error call (stage 8).
print("before")
error("boom")
print("not reached")
