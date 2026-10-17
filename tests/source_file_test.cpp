#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <cpplua/source_file.hpp>

using cpplua::skip_file_prefix;

TEST_CASE("a file without a prefix is lexed whole", "[source_file]") {
    CHECK(skip_file_prefix("").empty());
    CHECK(skip_file_prefix("return 1\n") == "return 1\n");
    CHECK(skip_file_prefix(" #x") == " #x"); // only at the very start
}

TEST_CASE("a '#' first line is skipped, but not its line break", "[source_file]") {
    CHECK(skip_file_prefix("#!/usr/bin/env lua\nreturn 1") == "\nreturn 1");
    CHECK(skip_file_prefix("# comment\r\nreturn 1") == "\nreturn 1");
    CHECK(skip_file_prefix("#\n") == "\n");
    CHECK(skip_file_prefix("#!lua").empty()); // nothing but the line
}

TEST_CASE("a UTF-8 byte order mark is skipped", "[source_file]") {
    CHECK(skip_file_prefix("\xEF\xBB\xBFreturn 1") == "return 1");
    CHECK(skip_file_prefix("\xEF\xBB\xBF#!lua\nreturn 1") == "\nreturn 1");
    CHECK(skip_file_prefix("\xEF\xBBreturn") == "\xEF\xBBreturn"); // not a whole mark
}
