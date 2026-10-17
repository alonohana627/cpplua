-- package.config: the separators used by package.path (directory, path, template, ...).
local dirsep, pathsep, mark = package.config:match("^(.)\n(.)\n(.)\n")
print(dirsep == "\\" or dirsep == "/")        --> true
print(pathsep, mark)                          --> ;	?
