-- Numbers follow the C library's locale, as printf and strtod do (P4.2, P4.3):
-- the same checks in "C", in a locale with a decimal comma, and back in "C".
local results = {}

local function check()
  local point = string.format("%.1f", 0.5):sub(2, 2)
  results[#results + 1] = tostring(3.5) == "3" .. point .. "5"
  results[#results + 1] = tostring(-0.0) == "-0" .. point .. "0"
  results[#results + 1] = 2.5 .. "" == "2" .. point .. "5"
  results[#results + 1] = tonumber("3" .. point .. "25") == 3.25
  results[#results + 1] = tonumber("3.25") == 3.25
  results[#results + 1] = load("return " .. string.format("%q", 0.1))() == 0.1
end

check()
local _ = os.setlocale("ptb", "numeric") or os.setlocale("pt_BR.UTF-8", "numeric")
  or os.setlocale("de_DE.UTF-8", "numeric")
check()
os.setlocale("C", "numeric")
check()

local text = {}
for i, ok in ipairs(results) do
  text[i] = tostring(ok)
end
return table.concat(text, " ")
