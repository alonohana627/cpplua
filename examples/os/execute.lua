-- os.execute([command]): runs a shell command; returns true/fail, "exit"/"signal" and a code.
print(os.execute())                           --> true (a shell is available)
print(os.execute("exit 3"))                   --> nil	exit	3
