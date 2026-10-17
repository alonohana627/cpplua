-- file:setvbuf(mode [, size]): sets buffering to "no", "full" or "line".
print(io.stdout:setvbuf("no"))                --> true
io.write("unbuffered: each write goes straight out\n")
io.stdout:setvbuf("line")
