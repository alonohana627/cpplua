#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <lua2/debug.hpp>

using lua2::token_dump;

TEST_CASE("token_dump lists one token per line", "[debug]") {
    const auto dump = token_dump("local x = 1");

    REQUIRE(dump.has_value());
    CHECK(*dump == "1     Local\n"
                   "1     Name         x\n"
                   "1     Assign\n"
                   "1     Integer      1\n"
                   "1     EndOfStream\n");
}

TEST_CASE("token_dump shows lines and every kind of value", "[debug]") {
    const auto dump = token_dump("s = 'a\\tb\\0'\nf = 2.5 @");

    REQUIRE(dump.has_value());
    CHECK(*dump == "1     Name         s\n"
                   "1     Assign\n"
                   "1     String       \"a\\tb\\x00\"\n"
                   "2     Name         f\n"
                   "2     Assign\n"
                   "2     Float        2.5\n"
                   "2     Other        \"@\"\n"
                   "2     EndOfStream\n");
}

TEST_CASE("token_dump shows floats that look like integers with .0", "[debug]") {
    const auto dump = token_dump("1e3 0.5 1e400");

    REQUIRE(dump.has_value());
    CHECK(*dump == "1     Float        1000.0\n"
                   "1     Float        0.5\n"
                   "1     Float        inf\n"
                   "1     EndOfStream\n");
}

TEST_CASE("token_dump returns the lexer's error", "[debug]") {
    const auto dump = token_dump("x = \"abc", "test.lua");

    REQUIRE_FALSE(dump.has_value());
    CHECK(dump.error() == "test.lua:1: unfinished string near <eof>");
}

TEST_CASE("ast_dump prints the tree", "[debug]") {
    const auto dump = lua2::ast_dump("-- nothing yet");

    REQUIRE(dump.has_value());
    CHECK(*dump == "Chunk\n");
}

TEST_CASE("ast_dump returns the parser's error", "[debug]") {
    const auto dump = lua2::ast_dump("x", "test.lua");

    REQUIRE_FALSE(dump.has_value());
    CHECK(dump.error() == "test.lua:1: syntax error near <eof>");
}

TEST_CASE("ast_dot_dump draws the tree as a DOT graph", "[debug]") {
    const auto dump = lua2::ast_dot_dump(R"(return 1, "a\\b")");

    REQUIRE(dump.has_value());
    CHECK(*dump == "digraph ast {\n"
                   "  ordering=out;\n"
                   "  node [shape=box, fontname=\"monospace\"];\n"
                   "  n0 [label=\"Chunk\"];\n"
                   "  n1 [label=\"Return\"];\n"
                   "  n0 -> n1;\n"
                   "  n2 [label=\"Integer 1\"];\n"
                   "  n1 -> n2;\n"
                   // print_ast shows the string as "a\\b"; DOT escapes each " and \ again.
                   "  n3 [label=\"String \\\"a\\\\\\\\b\\\"\"];\n"
                   "  n1 -> n3;\n"
                   "}\n");
}

TEST_CASE("ast_dot_dump returns the parser's error", "[debug]") {
    const auto dump = lua2::ast_dot_dump("x", "test.lua");

    REQUIRE_FALSE(dump.has_value());
    CHECK(dump.error() == "test.lua:1: syntax error near <eof>");
}
