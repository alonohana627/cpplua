#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <lua2/debug.hpp>
#include <lua2/interpreter.hpp>

// The VM (stage 5) against the rules of docs/spec/execution.md. Most
// behavior is covered by the my_lua programs, whose results come from the
// real Lua 5.4.9; these name the rules one by one.

namespace {

// Runs a chunk named "input" and returns its results joined with commas, or
// the error message.
std::string run(std::string_view source) {
    lua2::Interpreter interpreter;
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

// ---- E1. Chunks ----

TEST_CASE("E1.1 a chunk is a vararg function returning its results", "[vm]") {
    CHECK(run("return 1, 2, 3") == "1,2,3");
    CHECK(run("return ...").empty()); // no arguments given
    CHECK(run("local a, b = ... return a, b") == "nil,nil");
    CHECK(run("").empty());
}

TEST_CASE("E1.3 running a chunk again starts with fresh locals", "[vm]") {
    lua2::Interpreter interpreter;
    const std::string_view source = "local n = 0 n = n + 1 return n";
    CHECK(interpreter.run(source, "input")->front() == "1");
    CHECK(interpreter.run(source, "input")->front() == "1");
}

// ---- E3. Adjusting lists of values ----

TEST_CASE("E3.1 lists are adjusted to the variables", "[vm]") {
    CHECK(run("local a, b, c = 1, 2 return a, b, c") == "1,2,nil");
    CHECK(run("local a = 1, 2, 3 return a") == "1");
    CHECK(run("local function f() return 1, 2 end local a, b, c = f() return a, b, c") ==
          "1,2,nil");
}

TEST_CASE("E3.2 only a call at the end of a list gives all its values", "[vm]") {
    CHECK(run("local function f() return 1, 2 end return f(), f()") == "1,1,2");
    CHECK(run("local function f() return 1, 2 end return f(), 10") == "1,10");
}

TEST_CASE("E3.3 parentheses cut to one value", "[vm]") {
    CHECK(run("local function f() return 1, 2 end return (f())") == "1");
    CHECK(run("local function f(...) return (...) end return f(3, 4)") == "3");
    CHECK(run("local function f() end return (f())") == "nil");
}

TEST_CASE("E3.4 all values are evaluated before any assignment", "[vm]") {
    CHECK(run("local a, b = 1, 2 a, b = b, a return a, b") == "2,1");
    // (impl) assignments happen from right to left
    CHECK(run("local a a, a = 1, 2 return a") == "1");
}

// ---- E4. Evaluation order and folding ----

TEST_CASE("E4.2 and/or short-circuit", "[vm]") {
    CHECK(run("local n = 0 local function f() n = n + 1 return true end "
              "local _ = false and f() local _ = true or f() return n") == "0");
    CHECK(run("return nil and 1, false or 2, 1 and 2, nil or nil") == "nil,2,2,nil");
}

TEST_CASE("E4.4 an operation that would fail is not folded", "[vm]") {
    CHECK(run("return 2 // 0") == "input:1: attempt to divide by zero");
    CHECK(run("return 1 % 0") == "input:1: attempt to perform 'n%0'");
    CHECK(run("return 1 | 1.5") == "input:1: number has no integer representation");
    CHECK(run("return 2 // 0.0, -0.0") == "inf,-0.0");
}

// ---- E5. Control structures ----

TEST_CASE("E5.4 integer loops never overflow", "[vm]") {
    CHECK(run("local n = 0 for i = 9223372036854775806, 9223372036854775807 do n = n + 1 end "
              "return n") == "2");
    CHECK(run("local n = 0 for i = -9223372036854775807 - 1, -9223372036854775807, 1 do "
              "n = n + 1 end return n") == "2");
    CHECK(run("local n = 0 for i = 1, 0 do n = n + 1 end return n") == "0");
}

TEST_CASE("E5.5 for loop errors", "[vm]") {
    CHECK(run("for i = 1, 2, 0 do end") == "input:1: 'for' step is zero");
    CHECK(run("for i = 1.0, 2, 0 do end") == "input:1: 'for' step is zero");
    CHECK(run("local t for i = t, 2 do end") ==
          "input:1: bad 'for' initial value (number expected, got nil)");
    CHECK(run("for i = 1, 2, 'x' do end") ==
          "input:1: bad 'for' step (number expected, got string)");
}

TEST_CASE("E5.6 the loop variable is fresh in each iteration", "[vm]") {
    CHECK(run("local a, b for i = 1, 2 do local f = function() return i end "
              "if i == 1 then a = f else b = f end end return a(), b()") == "1,2");
}

TEST_CASE("E5.7 the generic for calls its iterator until nil", "[vm]") {
    CHECK(run("local function iter(limit, i) if i < limit then return i + 1, (i + 1) * 10 end "
              "end local s = 0 for i, v in iter, 3, 0 do s = s + i + v end return s") == "66");
    CHECK(run("for k in 42 do end") ==
          "input:1: attempt to call a number value (for iterator 'for iterator')");
}

// ---- E6. Functions and calls ----

TEST_CASE("E6.3 calling a non-function names it", "[vm]") {
    CHECK(run("local f f()") == "input:1: attempt to call a nil value (local 'f')");
    CHECK(run("local up local function g() up() end g()") ==
          "input:1: attempt to call a nil value (upvalue 'up')");
    CHECK(run("local s = 'x' return s()") == "input:1: attempt to call a string value (local 's')");
    CHECK(run("return (1)()") == "input:1: attempt to call a number value");
}

TEST_CASE("E6.5 tail calls run in constant space", "[vm]") {
    CHECK(run("local function loop(n) if n == 0 then return 'done' end return loop(n - 1) end "
              "return loop(10000000)") == "done");
}

TEST_CASE("E6.7 deep recursion is a stack overflow", "[vm]") {
    CHECK(run("local function f() return 1 + f() end return f()") == "input:1: stack overflow");
}

// ---- E7. Upvalues ----

TEST_CASE("E7.1 closures see the variable, not a copy", "[vm]") {
    CHECK(run("local x = 1 local function get() return x end x = 2 return get()") == "2");
}

TEST_CASE("E7.3 a captured variable outlives its function", "[vm]") {
    CHECK(run("local function make() local n = 41 return function() n = n + 1 return n end end "
              "local f = make() return f(), f()") == "42,43");
}

// ---- E8. Runtime errors ----

TEST_CASE("E8.1 errors carry the chunk name and line", "[vm]") {
    CHECK(run("local a\n\nreturn a + 1") ==
          "input:3: attempt to perform arithmetic on a nil value (local 'a')");
}

TEST_CASE("E8.2 messages say where the value came from", "[vm]") {
    CHECK(run("local up local function f() return #up end return f()") ==
          "input:1: attempt to get length of a nil value (upvalue 'up')");
    CHECK(run("local t return t.x") == "input:1: attempt to index a nil value (local 't')");
    CHECK(run("local x = 'a' .. {}") == "input:1: tables are not supported yet");
    CHECK(run("local a = true return a .. 'x'") ==
          "input:1: attempt to concatenate a boolean value (local 'a')");
    CHECK(run("local a = 1.5 return a & 1") ==
          "input:1: number (local 'a') has no integer representation");
}

TEST_CASE("E2.3 globals are fields of _ENV, the globals table", "[vm]") {
    CHECK(run("return x").empty() == false);
    CHECK(run("return x") == "nil");
    CHECK(run("x = 1 return x, _ENV.x, _ENV['x']") == "1,1,1");
    CHECK(run("function f() return 'f' end return f()") == "f");
    CHECK(run("x = 1 x = nil return x") == "nil");
    CHECK(run("undefined()") == "input:1: attempt to call a nil value (global 'undefined')");
    // A local _ENV is an ordinary variable.
    CHECK(run("local _ENV = 1 return x") ==
          "input:1: attempt to index a number value (local '_ENV')");
    CHECK(run("local _ENV = nil return x") ==
          "input:1: attempt to index a nil value (local '_ENV')");
    // Until stage 6 the globals table only takes string keys.
    CHECK(run("_ENV[1] = 2") == "input:1: table keys other than strings are not supported yet");
}

TEST_CASE("print writes its arguments like tostring, separated by tabs", "[vm][std]") {
    CHECK(run("return print == print, print ~= nil") == "true,true");
    CHECK(run("local r = print() return r") == "nil"); // print returns nothing
}

// ---- Syntax errors come through run() too ----

TEST_CASE("syntax errors stop before running", "[vm]") {
    CHECK(run("return +") == "input:1: unexpected symbol near '+'");
}

TEST_CASE("lua2 --bytecode lists like luac -l -l", "[vm][compiler]") {
    const auto listing = lua2::bytecode_dump("local x <const> = 2 return x * 3", "k.lua");
    REQUIRE(listing.has_value());
    // The constant is folded: x takes no register and 2 * 3 is 6.
    CHECK(*listing == "\n"
                      "main <k.lua:0,0> (4 instructions)\n"
                      "0+ params, 2 slots, 1 upvalue, 0 locals, 0 constants, 0 functions\n"
                      "\t1\t[1]\tVARARGPREP\t0\n"
                      "\t2\t[1]\tLOADI    \t0 6\n"
                      "\t3\t[1]\tRETURN   \t0 2 1\t; 1 out\n"
                      "\t4\t[1]\tRETURN   \t0 1 1\t; 0 out\n"
                      "constants (0):\n"
                      "locals (0):\n"
                      "upvalues (1):\n"
                      "\t0\t_ENV\t1\t0\n");
}
