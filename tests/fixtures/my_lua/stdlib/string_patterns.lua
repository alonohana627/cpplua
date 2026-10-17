-- Pattern items (stage 9): sets with ranges, classes and complements, ']'
-- and '-' and '^' inside sets, the quantifiers * + - ? (greedy and lazy),
-- %b balanced matches, back references %1, anchors, the frontier %f, and
-- the errors for malformed patterns.

-- Sets and ranges.
print(("hello123"):match("[a-z]+"), ("HELLO123"):match("[A-Z0-9]+"), ("abc"):match("[^a]+"))
print(("x]y"):match("[]]"), ("x-y"):match("[a-]"), ("x^y"):match("[%^]"), ("x^y"):match("[x^]+"))
print(("a-z"):match("[a%-z]+"), ("2+3=5"):match("[%d%+=]+"), ("tab\there"):match("[^%s]+$"))
print(("abcxyz"):gsub("[b-dx-y]", "."))
print(("Hello World"):gsub("[^%a ]", ""), ("a1b2c3"):gsub("[^%d]", ""))
print(("[test]"):match("^%[(.*)%]$"), ("a]b"):match("[^]]+"))

-- Quantifiers.
print(("aaa"):match("a*"), ("aaa"):match("a-") == "", ("aaa"):match("a+"), ("aaa"):match("a?"))
print(("<a><b>"):match("<.*>"), ("<a><b>"):match("<.->"))
print(("color colour"):gsub("colou?r", "C"))
print(("xyz"):match("x?y?z?w?"), ("123abc"):match("^%d*"), ("abc"):match("^%d*") == "")
print(("  x  "):match("^ *(.-) *$"), ("aXbXc"):match("(.-)X"), ("aXbXc"):match("(.*)X"))
print(("integer -42 here"):match("[+-]?%d+"), ("1.5e10"):match("^[%d.]+e?%d*$"))

-- Balanced matches.
print(("f(a, (b), c) + g()"):match("%b()"), ("{{}}{"):match("%b{}"))
print(("if x then y end"):match("%bie"), ("no close (here"):match("%b()"))
print(("[[a][b]]"):gsub("%b[]", "X"))

-- Back references.
print(('say "hi" now'):match('(["\'])(.-)%1'))
print(("abcabc"):match("(abc)%1"), ("abab"):match("^(ab)%1$"), ("abac"):match("^(ab)%1$"))
print(("x = [==[long]==]"):match("%[(=*)%[(.-)%]%1%]"))

-- Anchors: '^' only at the start, '$' only at the end; elsewhere literal.
print(("a^b"):match("a^b"), ("a$b"):match("a$b"), ("ab$"):match("b%$$"))
print(("abc"):find("^"), ("abc"):find("$"), ("abc"):match("^abc$"), ("abcd"):match("^abc$"))

-- Frontier patterns.
print(("hello world"):gsub("%f[%w]%w+", "[%0]"))
print(("one;two;;three"):gsub("%f[^;]", "|"))
print(("THE (quick) fox"):find("%f[%a]%a+%f[%A]"))

-- Errors for malformed patterns.
local bad = {"(", "%", "[a", "[a-", "%b", "%ba", "%f", "%fx", "(()", ")", "%1", "(a)%2", "[%", "(a%)"}
for _, p in ipairs(bad) do
  print(string.format("%-6s", p), pcall(string.match, "abc(a)", p))
end
-- string.find treats a pattern without any of ^$*+?.([%- as plain text, so
-- a lone ')' is found there instead of being an error.
print(string.find("abc(a)", ")"), pcall(string.gsub, "abc(a)", ")", ""))

return ("end"):match("(e)(n)(d)")
