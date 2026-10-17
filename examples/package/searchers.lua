-- package.searchers: the functions require uses, in order, to find a loader.
print(#package.searchers >= 2)                --> true

-- A custom searcher appended to the list.
table.insert(package.searchers, function(name)
  if name:match("^virtual%.") then
    return function() return { from = name } end, ":virtual:"
  end
  return "\n\tno virtual module '" .. name .. "'"
end)
print(require("virtual.thing").from)          --> virtual.thing
