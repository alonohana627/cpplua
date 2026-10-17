-- package.path: the search path require uses for Lua modules ("?" stands for the name).
print(type(package.path))                     --> string
for template in package.path:gmatch("[^;]+") do
  if template:find("?", 1, true) then print("has a template"); break end
end
--> has a template
