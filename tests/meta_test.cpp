#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <cpplua/interpreter.hpp>

// Metatables (stage 7) against the rules of docs/spec/metatables.md. The
// my_lua programs meta_*.lua, close_*.lua and gc_*.lua cover most of the
// behavior with the real Lua's output; these name the rules one by one.

namespace {

std::string run(std::string_view source) {
    cpplua::Interpreter interpreter;
    const auto results = interpreter.run(source, "input");
    if (!results) {
        return results.error();
    }
    std::string out;
    for (std::size_t n = 0; n < results->size(); ++n) {
        if (n > 0) {
            out += ',';
        }
        out += (*results)[n];
    }
    return out;
}

} // namespace

TEST_CASE("M1.1 setmetatable returns its table, getmetatable reads it back", "[meta]") {
    CHECK(run("local mt = {} local t = {} return setmetatable(t, mt) == t, getmetatable(t) == mt, "
              "getmetatable(setmetatable(t, nil))") == "true,true,nil");
    CHECK(run("return getmetatable('x').__index == string, getmetatable(1)") == "true,nil");
}

TEST_CASE("M1.3 __metatable protects a metatable", "[meta]") {
    CHECK(run("local t = setmetatable({}, {__metatable = 'no'}) "
              "return getmetatable(t), pcall(setmetatable, t, {})") ==
          "no,false,cannot change a protected metatable");
}

TEST_CASE("M1.5 setmetatable checks its arguments", "[meta]") {
    CHECK(run("return pcall(setmetatable, 1, {})") ==
          "false,bad argument #1 to 'setmetatable' (table expected, got number)");
    CHECK(run("return pcall(setmetatable, {}, 1)") ==
          "false,bad argument #2 to 'setmetatable' (nil or table expected, got number)");
}

TEST_CASE("M2.1 __index: a table, a function, a chain", "[meta]") {
    CHECK(
        run("local base = {x = 1} local t = setmetatable({}, {__index = base}) return t.x, t.y") ==
        "1,nil");
    CHECK(run("local t = setmetatable({}, {__index = function(t, k) return k .. '!' end}) "
              "return t.a, t[1]") == "a!,1!");
    CHECK(run("local a = {v = 'deep'} local b = setmetatable({}, {__index = a}) "
              "local c = setmetatable({}, {__index = b}) return c.v") == "deep");
    CHECK(run("local t = setmetatable({}, {__index = 5}) return t.x") ==
          "input:1: attempt to index a number value");
}

TEST_CASE("M2.2 __newindex: a function or a table, and present keys are raw", "[meta]") {
    CHECK(run("local log = {} local t = setmetatable({}, {__newindex = function(t, k, v) "
              "rawset(t, k, v * 2) end}) t.a = 1 t.a = 5 return t.a") == "5");
    CHECK(run("local store = {} local t = setmetatable({}, {__newindex = store}) t.x = 1 "
              "return rawget(t, 'x'), store.x") == "nil,1");
}

TEST_CASE("M2.3 a loop of __index tables is an error", "[meta]") {
    CHECK(run("local t = {} setmetatable(t, {__index = t}) return t.x") ==
          "input:1: '__index' chain too long; possible loop");
    CHECK(run("local t = {} setmetatable(t, {__newindex = t}) t.x = 1") ==
          "input:1: '__newindex' chain too long; possible loop");
}

TEST_CASE("M2.4 globals go through _ENV's metatable", "[meta]") {
    CHECK(run("setmetatable(_ENV, {__index = function(_, k) return 'default ' .. k end}) "
              "return undefined_name") == "default undefined_name");
}

TEST_CASE("M3.1 arithmetic metamethods, from either side", "[meta]") {
    CHECK(run("local mt = {__add = function(a, b) return 'add' end, "
              "__sub = function(a, b) return type(a) .. '-' .. type(b) end} "
              "local t = setmetatable({}, mt) return t + 1, 1 + t, t - 2, 2 - t") ==
          "add,add,table-number,number-table");
    CHECK(run("local t = setmetatable({}, {}) return t + 1") ==
          "input:1: attempt to perform arithmetic on a table value (local 't')");
}

TEST_CASE("M3.2 unary minus and bitwise not call with the operand twice", "[meta]") {
    CHECK(run("local t = setmetatable({}, {__unm = function(a, b) return a == b end, "
              "__bnot = function(a, b) return rawequal(a, b) end}) return -t, ~t") == "true,true");
}

TEST_CASE("M3.3 __concat, grouped to the right", "[meta]") {
    // 'a' .. 'b' .. t is 'a' .. ('b' .. t): __concat sees 'b' alone.
    CHECK(
        run("local t = setmetatable({}, {__concat = function(a, b) "
            "return (type(a) == 'table' and 'T' or a) .. '+' .. (type(b) == 'table' and 'T' or b) "
            "end}) return t .. 'x', 'x' .. t, 'a' .. 'b' .. t, 1 .. t") == "T+x,x+T,ab+T,1+T");
}

TEST_CASE("M3.4 __eq only for two different tables", "[meta]") {
    CHECK(run("local n = 0 local mt = {__eq = function() n = n + 1 return 1 end} "
              "local a, b = setmetatable({}, mt), setmetatable({}, mt) "
              "return a == b, a ~= b, a == a, a == 1, n") == "true,false,true,false,2");
}

TEST_CASE("M3.5 __lt and __le, and the __le fallback", "[meta]") {
    CHECK(run("local mt = {__lt = function(a, b) return a.v < b.v end} "
              "local a, b = setmetatable({v = 1}, mt), setmetatable({v = 2}, mt) "
              "return a < b, a > b, a <= b, b <= a") == "true,false,true,false");
    CHECK(run("return {} < {}") == "input:1: attempt to compare two table values");
    CHECK(run("return {} < 1") == "input:1: attempt to compare table with number");
}

TEST_CASE("M3.6 __len gives any value", "[meta]") {
    CHECK(run("local t = setmetatable({1, 2}, {__len = function() return 'many' end}) "
              "return #t") == "many");
}

TEST_CASE("M3.7 messages use __name", "[meta]") {
    CHECK(run("local p = setmetatable({}, {__name = 'Point'}) return p < p") ==
          "input:1: attempt to compare two Point values");
    CHECK(run("local p = setmetatable({}, {__name = 'Point'}) return p()") ==
          "input:1: attempt to call a Point value (local 'p')");
}

TEST_CASE("M4.1 __call gets the object first", "[meta]") {
    CHECK(run("local t = setmetatable({}, {__call = function(self, a, b) return a + b, self end}) "
              "local s, o = t(1, 2) return s, o == t") == "3,true");
    CHECK(run("local inner = setmetatable({}, {__call = function(self, x) return 'inner ' .. "
              "tostring(x) end}) local outer = setmetatable({}, {__call = inner}) "
              "return outer(1) == 'inner ' .. tostring(outer)") == "true");
}

TEST_CASE("M5.1 tostring uses __tostring and __name", "[meta]") {
    CHECK(run("return tostring(setmetatable({}, {__tostring = function() return 'hi' end}))") ==
          "hi");
    CHECK(
        run("return pcall(tostring, setmetatable({}, {__tostring = function() return {} end}))") ==
        "false,'__tostring' must return a string");
    CHECK(run("return tostring(setmetatable({}, {__name = 'Thing'})):sub(1, 7)") == "Thing: ");
}

TEST_CASE("M6.1 a to-be-closed value needs __close", "[meta]") {
    CHECK(run("local x <close> = 42") == "input:1: variable 'x' got a non-closable value");
    CHECK(run("do local x <close> = nil local y <close> = false end return 'ok'") == "ok");
}

TEST_CASE("M6.2 variables close in reverse order when their scope ends", "[meta]") {
    CHECK(run("local log = '' local function c(n) return setmetatable({}, {__close = function() "
              "log = log .. n end}) end do local a <close> = c('a') local b <close> = c('b') end "
              "return log") == "ba");
    CHECK(run("local log = '' local mt = {__close = function(_, e) log = log .. tostring(e) end} "
              "for i = 1, 3 do local x <close> = setmetatable({}, mt) if i == 2 then break end end "
              "return log") == "nilnil");
}

TEST_CASE("M6.3 an error passes its object to __close", "[meta]") {
    CHECK(run("local got local ok, e = pcall(function() local x <close> = setmetatable({}, "
              "{__close = function(_, err) got = err end}) error('boom', 0) end) "
              "return ok, e, got") == "false,boom,boom");
}

TEST_CASE("M7.2 finalizers run when collected and when closing", "[meta][gc]") {
    CHECK(run("local log = '' do local t = setmetatable({}, {__gc = function() log = log .. 'x' "
              "end}) end collectgarbage() collectgarbage() return log") == "x");
}

TEST_CASE("M7.3 weak tables lose collected entries", "[meta][gc]") {
    CHECK(run("local w = setmetatable({}, {__mode = 'k'}) w[{}] = 1 w['s'] = 2 collectgarbage() "
              "local n = 0 for k in pairs(w) do n = n + 1 end return n") == "1");
    CHECK(run("local w = setmetatable({}, {__mode = 'v'}) w[1] = {} w[2] = 'str' collectgarbage() "
              "return w[1], w[2]") == "nil,str");
}

TEST_CASE("M7.6 collectgarbage options", "[meta][gc]") {
    CHECK(
        run("return collectgarbage(), collectgarbage('count') > 0, collectgarbage('isrunning')") ==
        "0,true,true");
    CHECK(run("collectgarbage('stop') local r = collectgarbage('isrunning') "
              "collectgarbage('restart') return r") == "false");
    CHECK(run("return collectgarbage('generational'), collectgarbage('incremental')") ==
          "incremental,generational");
    CHECK(run("return pcall(collectgarbage, 'nope')") ==
          "false,bad argument #1 to 'collectgarbage' (invalid option 'nope')");
}
