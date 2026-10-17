-- Arithmetic, bitwise and comparison operators through the VM (stage 5),
-- with operands in registers, constants and immediates, so that every form
-- of each instruction runs (ADD, ADDI, ADDK, MMBIN, ...). No globals: _ENV
-- is nil until tables exist.
local i, f, s = 7, 2.5, "10"
local maxint = 9223372036854775807
local minint = -9223372036854775807 - 1
local inf = 1 / 0

local function values(...)
  return ...
end

return
  -- + - * with immediates, constants and registers
  i + 1, i + 1000, i + f, i - 1, 1 - i, 1000 - i, i * 3, f * 2, i * f,
  -- / is always a float; // and % round toward minus infinity
  i / 2, i // 2, -i // 2, i // -2, 7.5 // 2, i % 3, -i % 3, i % -3, 5.5 % 2, -5.5 % 2,
  -- ^ is always a float
  2 ^ 10, i ^ 0.5, i ^ -1,
  -- unary minus, folded and not
  -i, -f, - -i, -maxint, -minint,
  -- bitwise
  i & 3, i | 8, i ~ 5, ~i, 3 & 2.0, 1.0 | 2, i & 6.0,
  i << 2, i >> 1, 1 << 63, 1 << 64, -1 >> 1, i << -1, 3 << i, i >> -2,
  -- strings take part in arithmetic, not in bitwise
  s + 1, s * 2, "0x10" + 0, "1e2" + 0, s // 3, -s, s - "3.5",
  -- comparisons
  i < 10, i <= 7, i > 7, i >= 8, f < i, i < f, 1 < 1.5, i < inf, -inf < minint,
  2^53 < 2^53 + 1, maxint < 2^63, maxint + 0.0 == 2^63, minint == -2^63,
  "a" < "b", "abc" < "abd", "" < "a", "Z" < "a", "a\0b" < "a\0c",
  i == 7, i == 7.0, f == 2.5, s == "10", s == 10, i ~= 8, nil == false, 0 == -0,
  -- wrap-around and floats at the edges
  maxint + 1 == minint, minint - 1 == maxint, maxint * 2, minint // -1, minint % -1,
  0.1 + 0.2, 1e300 * 1e10, -1e300 * 1e10, 3 // 0.0, -3 // 0.0, -0.0, 0.0 * -1,
  5 % inf, -5 % inf, 9007199254740993 + 0.0,
  values(1, 2, 3)
