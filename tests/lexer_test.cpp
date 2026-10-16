#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "lexer/lexer.hpp"

using lua2::Lexer;
using lua2::TokenKind;

namespace {

// Returns the next token, failing the test if the lexer returned an error.
lua2::Token next_token(Lexer& lexer) {
    auto result = lexer.next();
    if (!result) {
        FAIL("unexpected lexer error: " << result.error().message);
    }
    return *std::move(result);
}

// Lexes all of `source` and returns every token kind, EndOfStream included.
std::vector<TokenKind> kinds(std::string_view source) {
    Lexer lexer{source};
    std::vector<TokenKind> result;
    for (;;) {
        const auto token = next_token(lexer);
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
        const auto token = next_token(lexer);
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
    const auto token = next_token(lexer);

    CHECK(token.kind == TokenKind::EndOfStream);
    CHECK(token.line == 1);
    CHECK(std::holds_alternative<std::monostate>(token.value));
}

TEST_CASE("L1.2 EndOfStream repeats after the end", "[lexer]") {
    Lexer lexer{""};
    const auto first = next_token(lexer);
    const auto second = next_token(lexer);
    const auto third = next_token(lexer);

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
    const auto token = next_token(lexer);

    CHECK(token.kind == kind);
    CHECK(token.line == 1);
    CHECK(std::holds_alternative<std::monostate>(token.value));
    CHECK(next_token(lexer).kind == TokenKind::EndOfStream);
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
    const auto token = next_token(lexer);

    CHECK(token.kind == TokenKind::Name);
    CHECK(token.line == 1);
    REQUIRE(std::holds_alternative<std::string>(token.value));
    CHECK(std::get<std::string>(token.value) == source);
    CHECK(next_token(lexer).kind == TokenKind::EndOfStream);
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
    const auto token = next_token(lexer);

    CHECK(token.kind == kind);
    CHECK(std::holds_alternative<std::monostate>(token.value));
    CHECK(next_token(lexer).kind == TokenKind::EndOfStream);
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
    const auto first = next_token(lexer);
    const auto second = next_token(lexer);

    CHECK(first == lua2::Token{.kind = TokenKind::Name, .line = 1, .value = std::string("first")});
    CHECK(second ==
          lua2::Token{.kind = TokenKind::Name, .line = 1, .value = std::string("second")});
}

// ---- Part 4: errors and stray characters ----

TEST_CASE("L10.1 the chunk name defaults to ?", "[lexer]") {
    CHECK(Lexer{""}.chunk_name() == "?");
    CHECK(Lexer{"", "test.lua"}.chunk_name() == "test.lua");
}

TEST_CASE("L9.1 a stray byte is an Other token holding that byte", "[lexer]") {
    // Built as std::string(1, byte) so that '\0' really is one zero byte.
    const auto byte =
        GENERATE(as<char>{}, '@', '$', '!', '`', '?', '\\', '\0', '\x01', '\x7f', '\x80', '\xff');
    CAPTURE(static_cast<int>(static_cast<unsigned char>(byte)));
    const std::string source(1, byte);

    Lexer lexer{source};
    const auto token = next_token(lexer);

    CHECK(token.kind == TokenKind::Other);
    CHECK(token.line == 1);
    CHECK(token.value == lua2::TokenValue{source});
    CHECK(next_token(lexer).kind == TokenKind::EndOfStream);
}

TEST_CASE("L9.1 a stray byte does not stop lexing", "[lexer]") {
    CHECK(kinds("a@b") ==
          std::vector{TokenKind::Name, TokenKind::Other, TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("x != y") == std::vector{TokenKind::Name, TokenKind::Other, TokenKind::Assign,
                                         TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L1.3 bytes above 127 are not letters", "[lexer]") {
    // "é" in UTF-8 is the two bytes C3 A9: two Other tokens, not part of a name.
    CHECK(kinds("a\xC3\xA9") ==
          std::vector{TokenKind::Name, TokenKind::Other, TokenKind::Other, TokenKind::EndOfStream});
}

// ---- Errors are returned, not thrown ----
//
// No real lexical error exists yet, so these use the temporary "not supported
// yet" error for a quote. Part 8 turns a lone quote into a real error
// (unfinished string), and these tests move to it then.

TEST_CASE("L10.1 an error is returned with chunk, line and near text", "[lexer]") {
    Lexer lexer{"x\n\n'", "test.lua"};
    REQUIRE(lexer.next().has_value()); // x

    const auto result = lexer.next();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "test.lua:3: not supported yet near '''");
    CHECK(result.error().line == 3);
}

TEST_CASE("L10.1 the error records where in the lexer it was made", "[lexer]") {
    Lexer lexer{"'"};
    const auto result = lexer.next();

    REQUIRE_FALSE(result.has_value());
    CHECK(std::string_view{result.error().origin.file_name()}.ends_with("lexer.cpp"));
}

TEST_CASE("L10.1 lexing stops at the first error", "[lexer]") {
    Lexer lexer{"' + x"};
    const auto first = lexer.next();
    const auto second = lexer.next();

    REQUIRE_FALSE(first.has_value());
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().message == first.error().message);
}
