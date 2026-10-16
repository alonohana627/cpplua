#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "lexer/token.hpp"
#include "parser/ast.hpp"
#include "parser/ast_printer.hpp"
#include "parser/parser.hpp"

using lua2::Parser;

namespace {

// Parses `source` and returns the printed tree, failing the test on an error.
std::string ast_of(std::string_view source) {
    Parser parser{source, "input"};
    auto ast = parser.parse_chunk();
    if (!ast) {
        FAIL("unexpected parse error: " << ast.error().message);
    }
    return lua2::print_ast(*ast);
}

// Parses `source`, which must fail, and returns the error message.
std::string parse_error(std::string_view source) {
    Parser parser{source, "input"};
    auto ast = parser.parse_chunk();
    REQUIRE_FALSE(ast.has_value());
    return ast.error().message;
}

} // namespace

// ---- Part 0: foundations ----

TEST_CASE("G2.1 an empty chunk is a Chunk with no statements", "[parser]") {
    const auto source = GENERATE(as<std::string_view>{}, "", "   ", "\n\n", "-- a comment",
                                 "--[[ a\nlong comment ]]");
    CAPTURE(source);

    CHECK(ast_of(source) == "Chunk\n");
}

TEST_CASE("G2.5 anything left after the chunk is '<eof> expected'", "[parser]") {
    // No statements exist yet, so any token is left over. Note that <eof> is
    // not quoted (G9.3).
    CHECK(parse_error("x") == "input:1: <eof> expected near 'x'");
    CHECK(parse_error("\n\n  end") == "input:3: <eof> expected near 'end'");
}

TEST_CASE("G9.2 near shows the token as written in the source", "[parser]") {
    const auto [source, near] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"0x10", "'0x10'"},   // not '16'
        {"1e3", "'1e3'"},     // not '1000.0'
        {"local", "'local'"}, // keywords quoted
        {"==", "'=='"},       // symbols quoted
        {"...", "'...'"},     //
        {"[[x]]", "'[[x]]'"}, // a long string as written, brackets included
        {"[==[a\nb]==]", "'[==[a\nb]==]'"},
        {R"("q")", R"('"q"')"},        // a short string with its own quotes
        {R"('q')", R"(''q'')"},        //
        {R"("\65\x42")", R"('"AB"')"}, // a short string after escapes, like Lua
        {"@", "'@'"},                  // a stray byte
        {"\x80", R"('<\128>')"},       // an unprintable stray byte
    }));
    CAPTURE(source);

    CHECK(parse_error(source) == "input:1: <eof> expected near " + std::string(near));
}

TEST_CASE("G9.4 a lexer error stops the parser with the lexer's message", "[parser]") {
    CHECK(parse_error("\"abc") == "input:1: unfinished string near <eof>");
    CHECK(parse_error("\n3x") == "input:2: malformed number near '3x'");
}

TEST_CASE("G9.1 the error records where in the parser it was made", "[parser]") {
    Parser parser{"x"};
    const auto ast = parser.parse_chunk();

    REQUIRE_FALSE(ast.has_value());
    CHECK(std::string_view{ast.error().origin.file_name()}.ends_with("parser.cpp"));
}

// ---- 'X' expected names (G9.3) ----

TEST_CASE("G9.3 how token kinds are named in 'X expected' messages", "[parser]") {
    using lua2::expected_text;
    using lua2::TokenKind;

    CHECK(expected_text(TokenKind::EndOfStream) == "<eof>");
    CHECK(expected_text(TokenKind::Name) == "<name>");
    CHECK(expected_text(TokenKind::String) == "<string>");
    CHECK(expected_text(TokenKind::End) == "'end'");
    CHECK(expected_text(TokenKind::Then) == "'then'");
    CHECK(expected_text(TokenKind::Assign) == "'='");
    CHECK(expected_text(TokenKind::RParen) == "')'");
    CHECK(expected_text(TokenKind::Ellipsis) == "'...'");
    CHECK(expected_text(TokenKind::DoubleColon) == "'::'");
}

// ---- The arena (lib/internal/parser/ast.hpp) ----

TEST_CASE("ExprId and StatId are different types", "[parser][ast]") {
    STATIC_CHECK_FALSE(std::is_convertible_v<lua2::ExprId, lua2::StatId>);
    STATIC_CHECK_FALSE(std::is_convertible_v<lua2::StatId, lua2::ExprId>);
    STATIC_CHECK(sizeof(lua2::StatId) == 4);
}

TEST_CASE("statement lists are stored back to back", "[parser][ast]") {
    using lua2::StatId;
    lua2::Ast ast;

    const std::array first{StatId{.index = 7}, StatId{.index = 8}};
    const std::array second{StatId{.index = 9}};
    const auto first_range = ast.add_statement_list(first);
    const auto second_range = ast.add_statement_list(second);
    const auto empty_range = ast.add_statement_list({});

    CHECK(first_range == lua2::IdRange<StatId>{.first = 0, .count = 2});
    CHECK(second_range == lua2::IdRange<StatId>{.first = 2, .count = 1});
    CHECK(empty_range.count == 0);

    const auto read_back = ast.statements(lua2::Block{.statements = first_range});
    REQUIRE(read_back.size() == 2);
    CHECK(read_back[0] == StatId{.index = 7});
    CHECK(read_back[1] == StatId{.index = 8});
}

// ---- Part 1: simple expressions and return ----

TEST_CASE("G4.1 every simple expression", "[parser]") {
    const auto [source, node] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"return nil", "Nil"},
        {"return true", "True"},
        {"return false", "False"},
        {"return ...", "Vararg"},
        {"return 42", "Integer 42"},
        {"return 0x10", "Integer 16"},
        {"return 0xffffffffffffffff", "Integer -1"},
        {"return 2.5", "Float 2.5"},
        {"return 1e3", "Float 1000.0"},
        {"return 1e400", "Float inf"},
        {"return 'hi'", R"(String "hi")"},
        {R"(return "a\tb\0")", R"(String "a\tb\x00")"},
        {"return [[long\nstring]]", R"(String "long\nstring")"},
        {"return ''", R"(String "")"},
    }));
    CAPTURE(source);

    CHECK(ast_of(source) == "Chunk\n  Return\n    " + std::string(node) + "\n");
}

TEST_CASE("G2.3 return with no values", "[parser]") {
    CHECK(ast_of("return") == "Chunk\n  Return\n");
    CHECK(ast_of("return;") == "Chunk\n  Return\n");
    CHECK(ast_of("  return  -- done") == "Chunk\n  Return\n");
}

TEST_CASE("G4.6 return with a list of values", "[parser]") {
    CHECK(ast_of("return 1, 'two', nil, ...") == "Chunk\n"
                                                 "  Return\n"
                                                 "    Integer 1\n"
                                                 "    String \"two\"\n"
                                                 "    Nil\n"
                                                 "    Vararg\n");
    CHECK(ast_of("return 1,\n2,\n3;") == "Chunk\n"
                                         "  Return\n"
                                         "    Integer 1\n"
                                         "    Integer 2\n"
                                         "    Integer 3\n");
}

TEST_CASE("G2.5 return must be the last statement", "[parser]") {
    CHECK(parse_error("return 1 return 2") == "input:1: <eof> expected near 'return'");
    CHECK(parse_error("return 1\nx") == "input:2: <eof> expected near 'x'");
    CHECK(parse_error("return;;") == "input:1: <eof> expected near ';'"); // only one ';'
    CHECK(parse_error("return end") == "input:1: <eof> expected near 'end'");
}

TEST_CASE("G4.7 a token that cannot start an expression", "[parser]") {
    CHECK(parse_error("return ,") == "input:1: unexpected symbol near ','");
    CHECK(parse_error("return 1,") == "input:1: unexpected symbol near <eof>");
    CHECK(parse_error("return 1, , 2") == "input:1: unexpected symbol near ','");
    CHECK(parse_error("return 1,\n\n)") == "input:3: unexpected symbol near ')'");
}

TEST_CASE("G4.1 nodes record their line", "[parser][ast]") {
    Parser parser{"\nreturn\n1,\n\n'x'"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto statements = ast->statements(ast->root());
    REQUIRE(statements.size() == 1);
    CHECK(ast->line(statements[0]) == 2);

    const auto& node = std::get<lua2::ReturnStat>(ast->statement(statements[0]));
    const auto values = ast->expressions(node.values);
    REQUIRE(values.size() == 2);
    CHECK(ast->line(values[0]) == 3);
    CHECK(ast->line(values[1]) == 5);
    CHECK(std::get<lua2::IntegerExpr>(ast->expression(values[0])).value == 1);
    CHECK(ast->text(std::get<lua2::StringExpr>(ast->expression(values[1])).text) == "x");
}

TEST_CASE("string values are stored once in the shared text buffer", "[parser][ast]") {
    lua2::Ast ast;
    const auto first = ast.add_text("hello");
    const auto second = ast.add_text("");
    const auto third = ast.add_text("world");

    CHECK(first == lua2::TextRange{.first = 0, .length = 5});
    CHECK(second.length == 0);
    CHECK(third == lua2::TextRange{.first = 5, .length = 5});
    CHECK(ast.text(first) == "hello");
    CHECK(ast.text(third) == "world");
}
