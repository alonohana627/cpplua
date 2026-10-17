-- os.getenv(varname): the value of an environment variable, or fail (nil).
local path = os.getenv("PATH")
print(type(path), #path > 0)                  --> string	true
print(os.getenv("SURELY_NOT_A_REAL_VARIABLE_12345"))   --> nil
