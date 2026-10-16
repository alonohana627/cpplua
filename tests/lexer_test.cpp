#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "lexer/lexer.hpp"

using lua2::Lexer;
using lua2::TokenKind;

namespace {

// Lexes all of `source` and returns every token kind, EndOfStream included.
std::vector<TokenKind> kinds(std::string_view source) {
    Lexer lexer{source};
    std::vector<TokenKind> result;
    for (;;) {
        const auto token = lexer.next();
        result.push_back(token.kind);
        if (token.kind == TokenKind::EndOfStream) {
            return result;
        }
    }
}

// Lexes all of `source` and returns the line of every token, EndOfStream included.
std::vector<int> lines(std::string_view source) {
    Lexer lexer{source};
    std::vector<int> result;
    for (;;) {
        const auto token = lexer.next();
        result.push_back(token.line);
        if (token.kind == TokenKind::EndOfStream) {
            return result;
        }
    }
}

struct SingleCase {
    std::string_view source;
    TokenKind kind;
};

} // namespace

TEST_CASE("L1.2 empty input gives EndOfStream on line 1", "[lexer]") {
    Lexer lexer{""};
    const auto token = lexer.next();

    CHECK(token.kind == TokenKind::EndOfStream);
    CHECK(token.line == 1);
    CHECK(std::holds_alternative<std::monostate>(token.value));
}

TEST_CASE("L1.2 EndOfStream repeats after the end", "[lexer]") {
    Lexer lexer{""};
    const auto first = lexer.next();
    const auto second = lexer.next();
    const auto third = lexer.next();

    CHECK(first.kind == TokenKind::EndOfStream);
    CHECK(second == first);
    CHECK(third == first);
}

TEST_CASE("Token equality compares kind, line and value", "[lexer]") {
    using lua2::Token;
    const Token a{.kind = TokenKind::EndOfStream, .line = 1, .value = {}};

    CHECK(a == Token{.kind = TokenKind::EndOfStream, .line = 1, .value = {}});
    CHECK(a != Token{.kind = TokenKind::EndOfStream, .line = 2, .value = {}});
    CHECK(a != Token{.kind = TokenKind::EndOfStream, .line = 1, .value = std::int64_t{1}});
}

// ---- Part 1: single-character tokens, whitespace and lines ----

TEST_CASE("L8.1 every single-character token", "[lexer]") {
    const auto [source, kind] = GENERATE(values<SingleCase>({
        {.source = "+", .kind = TokenKind::Plus},     {.source = "-", .kind = TokenKind::Minus},
        {.source = "*", .kind = TokenKind::Star},     {.source = "/", .kind = TokenKind::Slash},
        {.source = "%", .kind = TokenKind::Percent},  {.source = "^", .kind = TokenKind::Caret},
        {.source = "#", .kind = TokenKind::Hash},     {.source = "&", .kind = TokenKind::Ampersand},
        {.source = "~", .kind = TokenKind::Tilde},    {.source = "|", .kind = TokenKind::Pipe},
        {.source = "<", .kind = TokenKind::Less},     {.source = ">", .kind = TokenKind::Greater},
        {.source = "=", .kind = TokenKind::Assign},   {.source = "(", .kind = TokenKind::LParen},
        {.source = ")", .kind = TokenKind::RParen},   {.source = "{", .kind = TokenKind::LBrace},
        {.source = "}", .kind = TokenKind::RBrace},   {.source = "[", .kind = TokenKind::LBracket},
        {.source = "]", .kind = TokenKind::RBracket}, {.source = ";", .kind = TokenKind::Semicolon},
        {.source = ":", .kind = TokenKind::Colon},    {.source = ",", .kind = TokenKind::Comma},
        {.source = ".", .kind = TokenKind::Dot},
    }));
    CAPTURE(source);

    Lexer lexer{source};
    const auto token = lexer.next();

    CHECK(token.kind == kind);
    CHECK(token.line == 1);
    CHECK(std::holds_alternative<std::monostate>(token.value));
    CHECK(lexer.next().kind == TokenKind::EndOfStream);
}

TEST_CASE("L8.1 tokens in a row without spaces", "[lexer]") {
    CHECK(kinds("(){}") == std::vector{TokenKind::LParen, TokenKind::RParen, TokenKind::LBrace,
                                       TokenKind::RBrace, TokenKind::EndOfStream});
}

TEST_CASE("L3.1 whitespace separates tokens and is skipped", "[lexer]") {
    CHECK(kinds("( )") ==
          std::vector{TokenKind::LParen, TokenKind::RParen, TokenKind::EndOfStream});
    CHECK(kinds("  +  ") == std::vector{TokenKind::Plus, TokenKind::EndOfStream});
}

TEST_CASE("L1.3 every whitespace character is skipped", "[lexer]") {
    CHECK(kinds(" \t\v\f\n\r") == std::vector{TokenKind::EndOfStream});
    CHECK(kinds("+ \t\v\f+") ==
          std::vector{TokenKind::Plus, TokenKind::Plus, TokenKind::EndOfStream});
}

TEST_CASE("L2.1 each line break form counts as one line", "[lexer]") {
    const auto line_break = GENERATE(as<std::string_view>{}, "\n", "\r", "\r\n", "\n\r");
    CAPTURE(line_break.size());

    std::string source = "+";
    source += line_break;
    source += '+';

    CHECK(lines(source) == std::vector{1, 2, 2});
}

TEST_CASE("L2.1 repeated line breaks count separately", "[lexer]") {
    CHECK(lines("+\n\n+") == std::vector{1, 3, 3});
    CHECK(lines("+\r\r+") == std::vector{1, 3, 3});
    CHECK(lines("+\r\n\r\n+") == std::vector{1, 3, 3});
    CHECK(lines("+\n\r\n\r+") == std::vector{1, 3, 3});
}

TEST_CASE("L2.1 all four forms mixed", "[lexer]") {
    CHECK(lines("+\n-\r*\r\n/\n\r%") == std::vector{1, 2, 3, 4, 5, 5});
}

TEST_CASE("L2.2 a token gets the line it starts on", "[lexer]") {
    CHECK(lines("\n\n  (\n)") == std::vector{3, 4, 4});
}

TEST_CASE("L2.2 EndOfStream is on the last line", "[lexer]") {
    CHECK(lines("+\n") == std::vector{1, 2});
    CHECK(lines("\n\n\n") == std::vector{4});
}

// ---- Part 3: names and keywords ----

TEST_CASE("L4.1 a name carries its text", "[lexer]") {
    const auto source =
        GENERATE(as<std::string_view>{}, "x", "abc", "_", "_x1", "x9y", "_ENV", "A_long_Name_42");
    CAPTURE(source);

    Lexer lexer{source};
    const auto token = lexer.next();

    CHECK(token.kind == TokenKind::Name);
    CHECK(token.line == 1);
    REQUIRE(std::holds_alternative<std::string>(token.value));
    CHECK(std::get<std::string>(token.value) == source);
    CHECK(lexer.next().kind == TokenKind::EndOfStream);
}

TEST_CASE("L4.3 every keyword", "[lexer]") {
    // Spelled out here rather than read from the lexer's own table, so a typo
    // in the table cannot hide from the test.
    const auto [source, kind] = GENERATE(values<SingleCase>({
        {.source = "and", .kind = TokenKind::And},
        {.source = "break", .kind = TokenKind::Break},
        {.source = "do", .kind = TokenKind::Do},
        {.source = "else", .kind = TokenKind::Else},
        {.source = "elseif", .kind = TokenKind::ElseIf},
        {.source = "end", .kind = TokenKind::End},
        {.source = "false", .kind = TokenKind::False},
        {.source = "for", .kind = TokenKind::For},
        {.source = "function", .kind = TokenKind::Function},
        {.source = "goto", .kind = TokenKind::Goto},
        {.source = "if", .kind = TokenKind::If},
        {.source = "in", .kind = TokenKind::In},
        {.source = "local", .kind = TokenKind::Local},
        {.source = "nil", .kind = TokenKind::Nil},
        {.source = "not", .kind = TokenKind::Not},
        {.source = "or", .kind = TokenKind::Or},
        {.source = "repeat", .kind = TokenKind::Repeat},
        {.source = "return", .kind = TokenKind::Return},
        {.source = "then", .kind = TokenKind::Then},
        {.source = "true", .kind = TokenKind::True},
        {.source = "until", .kind = TokenKind::Until},
        {.source = "while", .kind = TokenKind::While},
    }));
    CAPTURE(source);

    Lexer lexer{source};
    const auto token = lexer.next();

    CHECK(token.kind == kind);
    CHECK(std::holds_alternative<std::monostate>(token.value));
    CHECK(lexer.next().kind == TokenKind::EndOfStream);
}

TEST_CASE("L4.2 keywords are case-sensitive", "[lexer]") {
    const auto source = GENERATE(as<std::string_view>{}, "While", "WHILE", "And", "NIL", "True");
    CAPTURE(source);

    CHECK(kinds(source) == std::vector{TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L4.4 a keyword inside a longer name is just a name", "[lexer]") {
    const auto source = GENERATE(as<std::string_view>{}, "whilex", "while_", "_while", "end1",
                                 "iff", "dot", "nil0", "functions", "localx", "orx", "do_");
    CAPTURE(source);

    CHECK(kinds(source) == std::vector{TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L4.5 names reserved by convention are ordinary names", "[lexer]") {
    CHECK(kinds("_ENV _G _VERSION") ==
          std::vector{TokenKind::Name, TokenKind::Name, TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L4.1 names end at operators and whitespace", "[lexer]") {
    CHECK(kinds("a+b") ==
          std::vector{TokenKind::Name, TokenKind::Plus, TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("t.x") ==
          std::vector{TokenKind::Name, TokenKind::Dot, TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("f(x,y)") == std::vector{TokenKind::Name, TokenKind::LParen, TokenKind::Name,
                                         TokenKind::Comma, TokenKind::Name, TokenKind::RParen,
                                         TokenKind::EndOfStream});
    CHECK(kinds("a\tb\nc") ==
          std::vector{TokenKind::Name, TokenKind::Name, TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L4.3 keywords and names together", "[lexer]") {
    CHECK(kinds("local x = y") == std::vector{TokenKind::Local, TokenKind::Name, TokenKind::Assign,
                                              TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("while x do end") == std::vector{TokenKind::While, TokenKind::Name, TokenKind::Do,
                                                 TokenKind::End, TokenKind::EndOfStream});
    CHECK(kinds("if a~=nil then") == std::vector{TokenKind::If, TokenKind::Name,
                                                 TokenKind::NotEqual, TokenKind::Nil,
                                                 TokenKind::Then, TokenKind::EndOfStream});
}

TEST_CASE("L2.2 names and keywords get their line", "[lexer]") {
    CHECK(lines("local\nx\n\ny") == std::vector{1, 2, 4, 4});
}

TEST_CASE("L4.1 two names lexed in a row keep their own text", "[lexer]") {
    Lexer lexer{"first second"};
    const auto first = lexer.next();
    const auto second = lexer.next();

    CHECK(first == lua2::Token{.kind = TokenKind::Name, .line = 1, .value = std::string("first")});
    CHECK(second ==
          lua2::Token{.kind = TokenKind::Name, .line = 1, .value = std::string("second")});
}
