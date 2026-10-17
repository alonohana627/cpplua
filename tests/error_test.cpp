#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <cpplua/interpreter.hpp>

// Errors (stage 8) against the rules of docs/spec/errors.md. The my_lua
// programs error_*.lua and err_*.lua cover most of the behavior with the
// real Lua's output; these name the rules one by one, and check the
// tracebacks, which the golden files cannot (the expected texts come from
// the real Lua 5.4.9).

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

// The error text `cpplua input` would print, with its traceback.
std::string run_with_traceback(std::string_view source) {
    cpplua::Interpreter interpreter;
    const auto results = interpreter.run(source, "input", true);
    return results ? std::string{"(no error)"} : results.error();
}

} // namespace

TEST_CASE("R1.1 any value can be an error object", "[error]") {
    CHECK(run("local t = {} local ok, e = pcall(error, t) return ok, e == t") == "false,true");
    CHECK(run("return pcall(error, nil)") == "false,nil");
    CHECK(run("return pcall(error, 42)") == "false,42");
    CHECK(run("error({})") == "(error object is a table value)");
    CHECK(run("error(setmetatable({}, {__tostring = function() return 'custom' end}))") ==
          "custom");
}

TEST_CASE("R1.2 error levels choose the position", "[error]") {
    CHECK(run("error('a')") == "input:1: a");
    CHECK(run("error('a', 0)") == "a");
    CHECK(run("local function f() error('b', 2) end\nf()") == "input:2: b");
    CHECK(run("return pcall(error, 'c')") == "false,c"); // level 1 is pcall, a C++ function
}

TEST_CASE("R1.3 assert returns its arguments or raises its message", "[error]") {
    CHECK(run("return assert(1, 2, 3)") == "1,2,3");
    CHECK(run("assert(false)") == "input:1: assertion failed!");
    CHECK(run("assert(nil, 'custom')") == "input:1: custom");
    CHECK(run("return pcall(assert)") == "false,bad argument #1 to 'assert' (value expected)");
}

TEST_CASE("R1.4 argument errors name the function as it was called", "[error]") {
    CHECK(run("local t = {} t.f = setmetatable t.f(1)") ==
          "input:1: bad argument #1 to 'f' (table expected, got number)");
    CHECK(run("local s = setmetatable s()") ==
          "input:1: bad argument #1 to 's' (table expected, got no value)");
}

TEST_CASE("R2.1 pcall returns true and the results, or false and the error", "[error]") {
    CHECK(run("return pcall(function(...) return ... end, 1, 2)") == "true,1,2");
    CHECK(run("return pcall(function() local x = nil return x.y end)") ==
          "false,input:1: attempt to index a nil value (local 'x')");
    CHECK(run("return pcall(5)") == "false,attempt to call a number value");
    CHECK(run("return pcall(pcall, error, 'e')") == "true,false,e");
}

TEST_CASE("R2.2 an error closes upvalues and to-be-closed variables on its way", "[error]") {
    CHECK(run("local f local ok = pcall(function() local x = 1 f = function() return x end "
              "error('e') end) return ok, f()") == "false,1");
}

TEST_CASE("R2.3 xpcall's handler sees the error first", "[error]") {
    CHECK(run("return xpcall(error, function(m) return 'handled ' .. m end, 'x', 0)") ==
          "false,handled x");
    CHECK(run("return xpcall(function(a, b) return a + b end, print, 1, 2)") == "true,3");
    CHECK(run("return pcall(xpcall, print)") ==
          "false,bad argument #2 to 'xpcall' (function expected, got no value)");
}

TEST_CASE("R2.4 an error in the handler ends in 'error in error handling'", "[error]") {
    CHECK(run("return xpcall(error, function(m) error(m) end, 'x')") ==
          "false,error in error handling");
}

TEST_CASE("R3.1 stack overflow is caught, and the stack recovers", "[error]") {
    CHECK(run("local function f() return 1 + f() end local ok, e = pcall(f) "
              "local ok2 = pcall(f) return ok, ok2, select(2, pcall(function() return 'fine' "
              "end))") == "false,false,fine");
}

TEST_CASE("R3.2 C++ calls nest at most 200 deep", "[error]") {
    CHECK(run("local t = setmetatable({}, {__index = function(t, k) return t[k] end})\n"
              "return pcall(function() return t.x end)") == "false,input:1: C stack overflow");
}

TEST_CASE("R4.1 debug.traceback lists the active functions", "[error]") {
    // Level 1 is the function calling traceback.
    CHECK(run("local function f() return debug.traceback('msg') end local s = f() return s") ==
          "msg\nstack traceback:\n\tinput:1: in local 'f'\n\tinput:1: in main chunk\n"
          "\t[C]: in ?");
    // f was entered by a tail call: no name, and a line saying so.
    CHECK(run("local function f() return debug.traceback('msg') end return f()") ==
          "msg\nstack traceback:\n\tinput:1: in function <input:1>\n\t(...tail calls...)\n"
          "\t[C]: in ?");
    CHECK(run("return debug.traceback({})").starts_with("table:"));
}

TEST_CASE("R2.5 R4.2 an uncaught error comes with a traceback", "[error]") {
    CHECK(run_with_traceback("local t = {}\n"
                             "function t.method() error('boom') end\n"
                             "local function caller() t.method() end\n"
                             "caller()") == "input:2: boom\n"
                                            "stack traceback:\n"
                                            "\t[C]: in function 'error'\n"
                                            "\tinput:2: in field 'method'\n"
                                            "\tinput:3: in local 'caller'\n"
                                            "\tinput:4: in main chunk\n"
                                            "\t[C]: in ?");
    CHECK(run_with_traceback("error({})") ==
          "(error object is a table value)\nstack traceback:\n\t[C]: in function 'error'\n"
          "\tinput:1: in main chunk\n\t[C]: in ?");
}

TEST_CASE("R4.3 deep tracebacks skip the middle", "[error]") {
    const std::string text = run_with_traceback(
        "local function f(n) if n == 0 then error('deep') end f(n - 1) end f(40)");
    CHECK(text.contains("\n\t...\t(skipping 22 levels)"));
}
