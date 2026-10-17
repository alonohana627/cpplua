-- The compiler, not the parser, stops a function with more than 255
-- upvalues; <const> locals with constant values do not count (issue #11).
local function locals(prefix, n, const)
    local s = {}
    for i = 1, n do
        s[#s + 1] = ("local %s%d%s = %d\n"):format(prefix, i, const and " <const>" or "", i)
    end
    return table.concat(s)
end
local function uses(n)
    local s = {}
    for i = 1, n do s[#s + 1] = "a" .. i .. " + b" .. i end
    return table.concat(s, " + ")
end
local function program(const)
    return locals("a", 130, const) .. "local function f()\n" .. locals("b", 130, const) ..
        "local function g() return " .. uses(130) .. " end\nreturn g()\nend\nreturn f()"
end
print(load(program(true))())
local _, message = load(program(false))
print(message:match("too many upvalues %(limit is 255%) in function at line %d+"))
