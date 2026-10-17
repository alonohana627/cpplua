-- os.execute gives the command's exit code, not the raw wait status the
-- C library's system() returns on POSIX (issue #7).
print(os.execute("exit 3"))
print(os.execute("exit 0"))
