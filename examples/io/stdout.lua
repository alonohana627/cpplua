-- io.stdout: the standard output file handle (the default output file).
io.stdout:write("written to stdout", "\n")    --> written to stdout
print(io.output() == io.stdout)               --> true
