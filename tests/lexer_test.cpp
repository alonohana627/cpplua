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
