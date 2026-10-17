-- io.flush(): pushes buffered writes on the default output file out now.
io.write("written before the flush")
io.flush()
print()                                       --> written before the flush
