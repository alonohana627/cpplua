-- Objects: classes through __index, method calls, and inheritance.
local Account = {}
Account.__index = Account

function Account.new(balance)
  return setmetatable({balance = balance}, Account)
end

function Account:deposit(v)
  self.balance = self.balance + v
end

function Account:get()
  return self.balance
end

local Savings = setmetatable({}, {__index = Account})
Savings.__index = Savings

function Savings.new(balance, rate)
  local s = Account.new(balance)
  s.rate = rate
  return setmetatable(s, Savings)
end

function Savings:interest()
  self:deposit(self:get() * self.rate)
end

local total = 0
for i = 1, 2000 do
  local a = Savings.new(i, 0)
  for _ = 1, 1000 do
    a:deposit(1)
    a:interest()
  end
  total = total + a:get()
end
return total
