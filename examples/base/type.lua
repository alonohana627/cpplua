-- type(v): the basic type name of v, as a string.
print(type(nil), type(true), type(1), type(1.5))      --> nil	boolean	number	number
print(type("s"), type({}), type(print), type(type))   --> string	table	function	function
print(type(io.stdout))                                --> userdata
