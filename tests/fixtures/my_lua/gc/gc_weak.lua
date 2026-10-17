-- Weak tables (stage 7): with __mode "k", "v" or "kv", entries whose weak
-- key or value is a collectable object reachable only through weak
-- references disappear at a collection. Strings, numbers and booleans are
-- values, not objects, and are never removed. The collector is stopped;
-- collection happens only at the explicit collectgarbage() calls.

collectgarbage("stop")

local function count(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

-- Weak keys.
local wk = setmetatable({}, {__mode = "k"})
local live_key = {}
wk[live_key] = "live"
local function add_dead_keys(t, n) for i = 1, n do t[{}] = i end end
add_dead_keys(wk, 10)
wk["str"] = {}            -- a string key stays, and so does its value
wk[1] = {}
wk[true] = {}
print("weak k before", count(wk))
collectgarbage()
print("weak k after", count(wk), wk[live_key], type(wk.str), type(wk[1]), type(wk[true]))

-- Weak values.
local wv = setmetatable({}, {__mode = "v"})
local live_value = {}
wv.live = live_value
local function add_dead_values(t, n) for i = 1, n do t[i] = {} end end
add_dead_values(wv, 10)
wv.s = "a string value"
wv.n = 42
wv.f = 1.5
print("weak v before", count(wv))
collectgarbage()
print("weak v after", count(wv), wv.live == live_value, wv.s, wv.n, wv.f, wv[1])

-- Weak keys and values.
local wkv = setmetatable({}, {__mode = "kv"})
local k1, v1 = {}, {}
wkv[k1] = "strong key, string value"
wkv["strong value"] = v1
local function add_dead(t)
  t[{}] = "dead key"
  t["dead value"] = {}
  t[{}] = {}
end
add_dead(wkv)
wkv["both strings"] = "kept"
collectgarbage()
print("weak kv", count(wkv), wkv[k1], wkv["strong value"] == v1, wkv["dead value"], wkv["both strings"])

-- Dropping the last strong reference later.
live_key = nil
live_value = nil
collectgarbage()
print("after dropping", count(wk), count(wv), wv.live)

-- A value kept alive by a strong table elsewhere stays.
local strong = {}
local cache = setmetatable({}, {__mode = "v"})
local function fill_cache()
  for i = 1, 5 do
    local obj = {id = i}
    cache[i] = obj
    if i % 2 == 0 then strong[#strong + 1] = obj end
  end
end
fill_cache()
collectgarbage()
local ids = 0
for _, v in pairs(cache) do ids = ids + v.id end
print("cache", count(cache), ids)

-- Ephemerons: in a weak-key table, a value that refers to its own key does
-- not keep the key alive.
local eph = setmetatable({}, {__mode = "k"})
local function add_ephemerons()
  for i = 1, 5 do
    local key = {}
    eph[key] = {owner = key, n = i}
  end
end
add_ephemerons()
local kept = {}
eph[kept] = {owner = kept}
print("ephemerons before", count(eph))
collectgarbage()
print("ephemerons after", count(eph), eph[kept].owner == kept)

-- A chain of ephemerons: k1 -> v1 refers to k2 -> v2 refers to k3; only
-- the first key is held, so the whole chain stays.
local chain = setmetatable({}, {__mode = "k"})
local head = {}
local function build_chain()
  local key = head
  for i = 1, 4 do
    local nextkey = {}
    chain[key] = {next = nextkey, i = i}
    key = nextkey
  end
end
build_chain()
collectgarbage()
print("chain kept", count(chain))
head = nil
collectgarbage()
print("chain dropped", count(chain))

-- Weak values that are objects with finalizers are removed before the
-- finalizer runs; weak keys are kept until the next collection.
local wv2 = setmetatable({}, {__mode = "v"})
local wk2 = setmetatable({}, {__mode = "k"})
local in_finalizer
local function finalizable()
  local o = setmetatable({}, {__gc = function(obj)
    in_finalizer = {wv2.obj == obj, wk2[obj]}
  end})
  wv2.obj = o
  wk2[o] = "key entry"
end
finalizable()
collectgarbage()
print("in finalizer", in_finalizer[1], in_finalizer[2])
print("after first", wv2.obj, count(wk2))
collectgarbage()
print("after second", count(wk2))

-- A __mode without "k" or "v" keeps everything.
local strong_mode = setmetatable({}, {__mode = "x"})
local function add_strong(t) t[{}] = {} end
add_strong(strong_mode)
collectgarbage()
print("mode x", count(strong_mode))

return count(wk), count(wv)
