-- ipairs(t): iterates t[1], t[2], ... up to the first nil.
local fruits = { "apple", "banana", "cherry", nil, "never seen" }
for i, v in ipairs(fruits) do
  print(i, v)
end
--> 1	apple
--> 2	banana
--> 3	cherry
