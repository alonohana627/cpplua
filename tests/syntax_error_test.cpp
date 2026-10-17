#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "syntax_error.hpp"

using cpplua::make_syntax_error;
using cpplua::near_char;

TEST_CASE("L10.1 a syntax error reads chunk:line: message near token", "[syntax-error]") {
    const auto error = make_syntax_error("input", 3, "unexpected symbol", "'@'");

    CHECK(error.message == "input:3: unexpected symbol near '@'");
    CHECK(error.line == 3);
}

TEST_CASE("L10.2 end of input is shown as <eof> without quotes", "[syntax-error]") {
    const auto error = make_syntax_error("test.lua", 12, "unfinished string", cpplua::near_eof);

    CHECK(error.message == "test.lua:12: unfinished string near <eof>");
}

TEST_CASE("a syntax error records the C++ location that created it", "[syntax-error]") {
    const auto error = make_syntax_error("?", 1, "message", "'x'");

    // The default argument captures the caller, which is this test.
    CHECK(std::string_view{error.origin.file_name()}.ends_with("syntax_error_test.cpp"));
    CHECK(error.origin.line() > 0);
}

TEST_CASE("L10.2 text read so far is shown in quotes", "[syntax-error]") {
    CHECK(cpplua::near_text("1e+") == "'1e+'");
    CHECK(cpplua::near_text("") == "''");
}

TEST_CASE("L10.2 a printable byte is shown in quotes", "[syntax-error]") {
    CHECK(near_char('@') == "'@'");
    CHECK(near_char('a') == "'a'");
    CHECK(near_char(' ') == "' '");
    CHECK(near_char('~') == "'~'");
}

TEST_CASE("L10.2 a non-printable byte is shown as its decimal value", "[syntax-error]") {
    CHECK(near_char('\0') == "'<\\0>'");
    CHECK(near_char('\t') == "'<\\9>'");
    CHECK(near_char(31) == "'<\\31>'");
    CHECK(near_char(127) == "'<\\127>'");
    CHECK(near_char(128) == "'<\\128>'");
    CHECK(near_char(255) == "'<\\255>'");
}

TEST_CASE("G9.5 an empty near leaves the near part out", "[syntax-error]") {
    const auto error = make_syntax_error("input", 2, "unknown attribute 'x'", "");

    CHECK(error.message == "input:2: unknown attribute 'x'");
}
