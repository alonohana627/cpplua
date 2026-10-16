#include <cstdint>
#include <limits>
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
// These use a lone quote, which is an unfinished string (L7.2).

TEST_CASE("L10.1 an error is returned with chunk, line and near text", "[lexer]") {
    Lexer lexer{"x\n\n'abc\n", "test.lua"};
    REQUIRE(lexer.next().has_value()); // x

    const auto result = lexer.next();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "test.lua:3: unfinished string near ''abc'");
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

// ---- Part 6: decimal numbers ----

namespace {

// Lexes `source`, which must be exactly one token, and returns that token.
lua2::Token only_token(std::string_view source) {
    Lexer lexer{source};
    auto token = next_token(lexer);
    CHECK(next_token(lexer).kind == TokenKind::EndOfStream);
    return token;
}

struct IntegerCase {
    std::string_view source;
    std::int64_t value;
};

struct FloatCase {
    std::string_view source;
    double value;
};

} // namespace

TEST_CASE("L5.4 L5.6 decimal integers", "[lexer][number]") {
    const auto [source, value] = GENERATE(values<IntegerCase>({
        {.source = "0", .value = 0},
        {.source = "3", .value = 3},
        {.source = "345", .value = 345},
        {.source = "007", .value = 7}, // leading zeros do not mean octal
        {.source = "9223372036854775807", .value = INT64_MAX},
    }));
    CAPTURE(source);

    const auto token = only_token(source);

    CHECK(token.kind == TokenKind::Integer);
    CHECK(token.value == lua2::TokenValue{value});
}

TEST_CASE("L5.4 L5.6 decimal floats", "[lexer][number]") {
    const auto [source, value] = GENERATE(values<FloatCase>({
        {.source = "3.0", .value = 3.0},
        {.source = "3.1416", .value = 3.1416},
        {.source = "314.16e-2", .value = 314.16e-2},
        {.source = "0.31416E1", .value = 0.31416E1},
        {.source = "34e1", .value = 34e1},
        {.source = "1e+2", .value = 1e+2},
        {.source = ".5", .value = 0.5},
        {.source = "5.", .value = 5.0},
        {.source = "0.1", .value = 0.1},
        {.source = "1e10", .value = 1e10},
    }));
    CAPTURE(source);

    const auto token = only_token(source);

    CHECK(token.kind == TokenKind::Float);
    CHECK(token.value == lua2::TokenValue{value}); // exact: same rounding as the C++ literal
}

TEST_CASE("L5.6 a point or an exponent makes a float", "[lexer][number]") {
    CHECK(only_token("1").kind == TokenKind::Integer);
    CHECK(only_token("1.").kind == TokenKind::Float);
    CHECK(only_token("1.0").kind == TokenKind::Float);
    CHECK(only_token("1e0").kind == TokenKind::Float);
    CHECK(only_token("1e2").value == lua2::TokenValue{100.0});
}

TEST_CASE("L5.7 a decimal integer too large for 64 bits becomes a float", "[lexer][number]") {
    const auto max_plus_one = only_token("9223372036854775808");
    CHECK(max_plus_one.kind == TokenKind::Float);
    CHECK(max_plus_one.value == lua2::TokenValue{9223372036854775808.0});

    const auto huge = only_token("100000000000000000000");
    CHECK(huge.kind == TokenKind::Float);
    CHECK(huge.value == lua2::TokenValue{1e20});
}

TEST_CASE("L5.9 floats too large are infinity, too small are zero", "[lexer][number]") {
    CHECK(only_token("1e400").value == lua2::TokenValue{std::numeric_limits<double>::infinity()});
    CHECK(only_token("1.8e308").value == lua2::TokenValue{std::numeric_limits<double>::infinity()});
    CHECK(only_token("1e-400").value == lua2::TokenValue{0.0});

    // 400 zeros after the point, with no exponent, still underflows to 0.
    const std::string tiny = "0." + std::string(400, '0') + "1";
    CHECK(only_token(tiny).value == lua2::TokenValue{0.0});

    // The largest double and the smallest subnormal are still in range.
    CHECK(only_token("1.7976931348623157e308").value ==
          lua2::TokenValue{std::numeric_limits<double>::max()});
    CHECK(only_token("4.9e-324").value ==
          lua2::TokenValue{std::numeric_limits<double>::denorm_min()});
}

TEST_CASE("L5.10 numerals have no sign", "[lexer][number]") {
    CHECK(kinds("-5") == std::vector{TokenKind::Minus, TokenKind::Integer, TokenKind::EndOfStream});
    CHECK(kinds("+5") == std::vector{TokenKind::Plus, TokenKind::Integer, TokenKind::EndOfStream});
    // So the smallest integer cannot be written directly: this is minus a float.
    CHECK(kinds("-9223372036854775808") ==
          std::vector{TokenKind::Minus, TokenKind::Float, TokenKind::EndOfStream});
}

TEST_CASE("L8.4 dots next to numbers", "[lexer][number]") {
    CHECK(kinds("a..5") == std::vector{TokenKind::Name, TokenKind::Concat, TokenKind::Integer,
                                       TokenKind::EndOfStream});
    CHECK(kinds("...5") ==
          std::vector{TokenKind::Ellipsis, TokenKind::Integer, TokenKind::EndOfStream});
    CHECK(kinds("t.x") ==
          std::vector{TokenKind::Name, TokenKind::Dot, TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("1 .. 2") == std::vector{TokenKind::Integer, TokenKind::Concat, TokenKind::Integer,
                                         TokenKind::EndOfStream});
}

TEST_CASE("L5.4 numbers end at operators and whitespace", "[lexer][number]") {
    CHECK(kinds("1+2") == std::vector{TokenKind::Integer, TokenKind::Plus, TokenKind::Integer,
                                      TokenKind::EndOfStream});
    CHECK(kinds("x=1.5,2") == std::vector{TokenKind::Name, TokenKind::Assign, TokenKind::Float,
                                          TokenKind::Comma, TokenKind::Integer,
                                          TokenKind::EndOfStream});
    CHECK(kinds("2e-3*4") == std::vector{TokenKind::Float, TokenKind::Star, TokenKind::Integer,
                                         TokenKind::EndOfStream});
}

TEST_CASE("L2.2 numbers get their line", "[lexer][number]") {
    CHECK(lines("1\n2.5\n\n.5") == std::vector{1, 2, 4, 4});
}

TEST_CASE("L5.3 an exponent without digits is a malformed number", "[lexer][number]") {
    const auto [source, near] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"1e", "'1e'"},
        {"1e+", "'1e+'"},
        {"1E-", "'1E-'"},
        {"2.5e", "'2.5e'"},
        {"1e+ 2", "'1e+'"},
    }));
    CAPTURE(source);

    Lexer lexer{source, "input"};
    const auto result = lexer.next();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "input:1: malformed number near " + std::string(near));
}

// ---- Part 7: hex numbers and malformed numbers ----

TEST_CASE("L5.5 hex integers", "[lexer][number]") {
    const auto [source, value] = GENERATE(values<IntegerCase>({
        {.source = "0x0", .value = 0},
        {.source = "0xff", .value = 255},
        {.source = "0XFF", .value = 255},
        {.source = "0xA", .value = 10},
        {.source = "0xBEBADA", .value = 0xBEBADA},
        {.source = "0x1e", .value = 30}, // in hex, e is a digit, not an exponent
        {.source = "0x00000010", .value = 16},
        {.source = "0x7fffffffffffffff", .value = INT64_MAX},
    }));
    CAPTURE(source);

    const auto token = only_token(source);

    CHECK(token.kind == TokenKind::Integer);
    CHECK(token.value == lua2::TokenValue{value});
}

TEST_CASE("L5.8 hex integers wrap around modulo 2^64", "[lexer][number]") {
    const auto [source, value] = GENERATE(values<IntegerCase>({
        {.source = "0xffffffffffffffff", .value = -1},
        {.source = "0x8000000000000000", .value = INT64_MIN},
        {.source = "0x10000000000000000", .value = 0},
        {.source = "0x1ffffffffffffffff", .value = -1},
        {.source = "0x10000000000000001", .value = 1},
    }));
    CAPTURE(source);

    const auto token = only_token(source);

    CHECK(token.kind == TokenKind::Integer);
    CHECK(token.value == lua2::TokenValue{value});
}

TEST_CASE("L5.5 hex floats", "[lexer][number]") {
    const auto [source, value] = GENERATE(values<FloatCase>({
        {.source = "0x0.1E", .value = 0.1171875},
        {.source = "0xA23p-4", .value = 162.1875},
        {.source = "0X1.921FB54442D18P+1", .value = 3.141592653589793},
        {.source = "0x.8", .value = 0.5},
        {.source = "0x8.", .value = 8.0},
        {.source = "0x1p4", .value = 16.0},
        {.source = "0x1P-1", .value = 0.5},
        {.source = "0x1p+1", .value = 2.0},
        {.source = "0x10p0", .value = 16.0},
    }));
    CAPTURE(source);

    const auto token = only_token(source);

    CHECK(token.kind == TokenKind::Float);
    CHECK(token.value == lua2::TokenValue{value});
}

TEST_CASE("L5.9 hex floats out of range are infinity", "[lexer][number]") {
    CHECK(only_token("0x1p99999").value ==
          lua2::TokenValue{std::numeric_limits<double>::infinity()});
}

TEST_CASE("L5.2 L5.3 malformed numbers", "[lexer][number]") {
    const auto [source, near] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"3x", "'3x'"},           // a letter touching the number
        {"3..2", "'3..2'"},       // read whole, then rejected
        {"1.2.3", "'1.2.3'"},     // two points
        {"3a", "'3a'"},           // hex digits are read even in a decimal number
        {"08z", "'08z'"},         //
        {"2_", "'2_'"},           // '_' counts as a letter
        {"1.5e2.3", "'1.5e2.3'"}, // a point after the exponent
        {"0x", "'0x'"},           // no hex digits
        {"0X", "'0X'"},           //
        {"0xg", "'0xg'"},         // not a hex digit
        {"0x.p1", "'0x.p1'"},     // a point but no digits
        {"0xp1", "'0xp1'"},       // an exponent but no digits
        {"0x1p", "'0x1p'"},       // exponent without digits
        {"0x1.2.3", "'0x1.2.3'"}, // two points in hex
        {"00x1", "'00x'"},        // only a single leading 0 starts hex
    }));
    CAPTURE(source);

    Lexer lexer{source, "input"};
    const auto result = lexer.next();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "input:1: malformed number near " + std::string(near));
}

TEST_CASE("L5.2 the number ends at the first character that cannot belong", "[lexer][number]") {
    CHECK(kinds("0x10+1") == std::vector{TokenKind::Integer, TokenKind::Plus, TokenKind::Integer,
                                         TokenKind::EndOfStream});
    CHECK(kinds("0x1p4*2") == std::vector{TokenKind::Float, TokenKind::Star, TokenKind::Integer,
                                          TokenKind::EndOfStream});
    CHECK(kinds("1e5-2") == // '-' only belongs to a number right after the exponent letter
          std::vector{TokenKind::Float, TokenKind::Minus, TokenKind::Integer,
                      TokenKind::EndOfStream});
    CHECK(kinds("0xff-1") == std::vector{TokenKind::Integer, TokenKind::Minus, TokenKind::Integer,
                                         TokenKind::EndOfStream});
}

// ---- Part 5: short comments ----

TEST_CASE("L3.2 a comment is skipped", "[lexer][comment]") {
    CHECK(kinds("a -- c") == std::vector{TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("--only a comment") == std::vector{TokenKind::EndOfStream});
    CHECK(kinds("--") == std::vector{TokenKind::EndOfStream});
    CHECK(kinds("---") == std::vector{TokenKind::EndOfStream});
}

TEST_CASE("L3.4 a short comment ends at the line break", "[lexer][comment]") {
    CHECK(kinds("x = 1 -- set x\ny = 2") ==
          std::vector{TokenKind::Name, TokenKind::Assign, TokenKind::Integer, TokenKind::Name,
                      TokenKind::Assign, TokenKind::Integer, TokenKind::EndOfStream});
    CHECK(kinds("a--c\nb") ==
          std::vector{TokenKind::Name, TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L3.4 the line break after a comment is still counted", "[lexer][comment]") {
    const auto line_break = GENERATE(as<std::string_view>{}, "\n", "\r", "\r\n", "\n\r");
    CAPTURE(line_break.size());

    std::string source = "a -- comment";
    source += line_break;
    source += 'b';

    CHECK(lines(source) == std::vector{1, 2, 2});
}

TEST_CASE("L3.4 text that is not a long bracket is a short comment", "[lexer][comment]") {
    CHECK(kinds("--[ x\ny") == std::vector{TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("--[=x\ny") == std::vector{TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("--[==x\ny") == std::vector{TokenKind::Name, TokenKind::EndOfStream});
}

TEST_CASE("L3.6 a comment may end at the end of the input", "[lexer][comment]") {
    CHECK(lines("a\n-- last line") == std::vector{1, 2});
}

TEST_CASE("L8.3 one '-' is minus, two start a comment", "[lexer][comment]") {
    CHECK(kinds("a - b") ==
          std::vector{TokenKind::Name, TokenKind::Minus, TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("a-b") ==
          std::vector{TokenKind::Name, TokenKind::Minus, TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("a - -b") == std::vector{TokenKind::Name, TokenKind::Minus, TokenKind::Minus,
                                         TokenKind::Name, TokenKind::EndOfStream});
    CHECK(kinds("1--2") == std::vector{TokenKind::Integer, TokenKind::EndOfStream});
}

TEST_CASE("L3.2 a comment hides everything on its line", "[lexer][comment]") {
    // Even text that would be an error: an unfinished string, a bad number.
    CHECK(kinds("-- \"unfinished 3x 0xg @") == std::vector{TokenKind::EndOfStream});
}

// ---- Part 8: short strings, simple escapes ----

namespace {

// Lexes `source`, which must be exactly one String token, and returns its bytes.
std::string string_value(std::string_view source) {
    const auto token = only_token(source);
    REQUIRE(token.kind == TokenKind::String);
    return std::get<std::string>(token.value);
}

// Lexes `source`, which must fail on its first token, and returns the message.
std::string error_message(std::string_view source) {
    Lexer lexer{source, "input"};
    const auto result = lexer.next();
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

} // namespace

TEST_CASE("L7.1 strings in double and single quotes", "[lexer][string]") {
    CHECK(string_value(R"("abc")") == "abc");
    CHECK(string_value("'abc'") == "abc");
    CHECK(string_value(R"("")").empty());
    CHECK(string_value("''").empty());
    CHECK(string_value(R"("hello, world")") == "hello, world");
}

TEST_CASE("L7.1 the other quote may appear unescaped", "[lexer][string]") {
    CHECK(string_value(R"("it's")") == "it's");
    CHECK(string_value(R"('say "hi"')") == R"(say "hi")");
}

TEST_CASE("L7.3 named escapes", "[lexer][string]") {
    const auto [source, value] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {R"("\a")", "\a"},
        {R"("\b")", "\b"},
        {R"("\f")", "\f"},
        {R"("\n")", "\n"},
        {R"("\r")", "\r"},
        {R"("\t")", "\t"},
        {R"("\v")", "\v"},
        {R"("\\")", "\\"},
        {R"("\"")", "\""},
        {R"("\'")", "'"},
        {R"('\'')", "'"},
        {R"("a\tb\nc")", "a\tb\nc"},
    }));
    CAPTURE(source);

    CHECK(string_value(source) == value);
}

TEST_CASE("L7.3 a backslash before a line break is one newline", "[lexer][string]") {
    const auto line_break = GENERATE(as<std::string_view>{}, "\n", "\r", "\r\n", "\n\r");
    CAPTURE(line_break.size());

    std::string source = "\"a\\";
    source += line_break;
    source += "b\" x";

    Lexer lexer{source};
    const auto string = next_token(lexer);
    const auto name = next_token(lexer);

    CHECK(string.value == lua2::TokenValue{std::string("a\nb")});
    CHECK(string.line == 1); // the line the string starts on
    CHECK(name.line == 2);   // the escaped line break was counted
}

TEST_CASE("L7.7 other bytes stand for themselves", "[lexer][string]") {
    CHECK(string_value("\"a\xC3\xA9z\"") == "a\xC3\xA9z"); // UTF-8 bytes
    const std::string with_zero{"\"a\0b\"", 5};
    CHECK(string_value(with_zero) == std::string("a\0b", 3)); // a raw zero byte
    CHECK(string_value(R"("-- not a comment")") == "-- not a comment");
}

TEST_CASE("L7.2 a raw line break makes an unfinished string", "[lexer][string]") {
    CHECK(error_message("\"abc\ndef\"") == "input:1: unfinished string near '\"abc'");
    CHECK(error_message("'abc\r'") == "input:1: unfinished string near ''abc'");
    // The near text shows the bytes after escapes, as Lua does.
    CHECK(error_message("\"a\\tb\n") == "input:1: unfinished string near '\"a\tb'");
}

TEST_CASE("L7.2 end of input makes an unfinished string near <eof>", "[lexer][string]") {
    CHECK(error_message("\"abc") == "input:1: unfinished string near <eof>");
    CHECK(error_message("'") == "input:1: unfinished string near <eof>");
    CHECK(error_message("\"abc\\") == "input:1: unfinished string near <eof>");
}

TEST_CASE("L7.6 an unknown escape is an error", "[lexer][string]") {
    CHECK(error_message(R"("abc\q")") == R"(input:1: invalid escape sequence near '"abc\q')");
    CHECK(error_message(R"('\c')") == R"(input:1: invalid escape sequence near ''\c')");
}

TEST_CASE("L7.1 strings next to other tokens", "[lexer][string]") {
    CHECK(kinds(R"(print("hi"))") == std::vector{TokenKind::Name, TokenKind::LParen,
                                                 TokenKind::String, TokenKind::RParen,
                                                 TokenKind::EndOfStream});
    CHECK(kinds(R"("a".."b")") == std::vector{TokenKind::String, TokenKind::Concat,
                                              TokenKind::String, TokenKind::EndOfStream});
    CHECK(kinds(R"(x = 'y' -- comment)") == std::vector{TokenKind::Name, TokenKind::Assign,
                                                        TokenKind::String, TokenKind::EndOfStream});
}

// ---- Part 9: numeric escapes, \u{} and \z ----

namespace {

using EscapeCase = std::pair<std::string_view, std::string_view>;

} // namespace

// The sv suffix keeps every byte of a literal, including a '\0' inside it:
// "a\0b"sv is 3 bytes, while a plain "a\0b" would stop at the zero.
using namespace std::string_view_literals;

TEST_CASE("L7.3 \\xXX is a byte from two hex digits", "[lexer][string]") {
    const auto [source, value] = GENERATE(values<EscapeCase>({
        {R"("\x41")", "A"},
        {R"("\x4a\x4A")", "JJ"},
        {R"("\x41\x42C")", "ABC"},
        {R"("\x00")", "\0"sv},
        {R"("\xff")", "\xff"},
        {R"("\x7F")", "\x7f"},
    }));
    CAPTURE(source);

    CHECK(string_value(source) == value);
}

TEST_CASE("L7.6 \\x needs exactly two hex digits", "[lexer][string]") {
    CHECK(error_message(R"("\x4")") == R"(input:1: hexadecimal digit expected near '"\x4"')");
    CHECK(error_message(R"("\xg1")") == R"(input:1: hexadecimal digit expected near '"\xg')");
    CHECK(error_message(R"("ab\x")") == R"(input:1: hexadecimal digit expected near '"ab\x"')");
    CHECK(error_message(R"("\x)") == R"(input:1: hexadecimal digit expected near '"\x')");
}

TEST_CASE("L7.4 \\ddd is a byte from up to three decimal digits", "[lexer][string]") {
    const auto [source, value] = GENERATE(values<EscapeCase>({
        {R"("\65")", "A"},
        {R"("\065")", "A"},
        {R"("\0651")", "A1"}, // at most three digits are read
        {R"("\9")", "\t"},
        {R"("\09")", "\t"},
        {R"("\009")", "\t"},
        {R"("\0")", "\0"sv},
        {R"("a\0b")", "a\0b"sv},
        {R"("\255")", "\xff"},
        {R"("\72\105")", "Hi"},
    }));
    CAPTURE(source);

    CHECK(string_value(source) == value);
}

TEST_CASE("L7.6 \\ddd above 255 is an error", "[lexer][string]") {
    CHECK(error_message(R"("\256")") == R"(input:1: decimal escape too large near '"\256"')");
    CHECK(error_message(R"("x\999z")") == R"(input:1: decimal escape too large near '"x\999z')");
    CHECK(error_message(R"("\300)") == R"(input:1: decimal escape too large near '"\300')");
}

TEST_CASE("L7.3 \\z skips the whitespace that follows", "[lexer][string]") {
    CHECK(string_value(R"("a\z    b")") == "ab");
    CHECK(string_value(R"("a\zb")") == "ab");
    CHECK(string_value(R"("a\z")") == "a");
    CHECK(string_value("\"a\\z \t\v\f b\"") == "ab");
    CHECK(string_value("\"a\\z  \n\n   b c\"") == "ab c"); // only the whitespace right after
}

TEST_CASE("L7.3 \\z counts the lines it skips", "[lexer][string]") {
    Lexer lexer{"\"a\\z\n\r\n  b\" x"};
    const auto string = next_token(lexer);
    const auto name = next_token(lexer);

    CHECK(string.value == lua2::TokenValue{std::string("ab")});
    CHECK(string.line == 1);
    CHECK(name.line == 3);
}

TEST_CASE("L7.5 \\u{XXX} is the UTF-8 encoding of a code point", "[lexer][string]") {
    const auto [source, value] = GENERATE(values<EscapeCase>({
        {R"("\u{48}")", "H"},
        {R"("\u{0}")", "\0"sv},
        {R"("\u{000041}")", "A"}, // leading zeros
        {R"("\u{7F}")", "\x7f"},  // the largest 1-byte value
        {R"("\u{80}")", "\xC2\x80"},
        {R"("\u{E9}")", "\xC3\xA9"}, // é
        {R"("\u{7FF}")", "\xDF\xBF"},
        {R"("\u{800}")", "\xE0\xA0\x80"},
        {R"("\u{20AC}")", "\xE2\x82\xAC"}, // €
        {R"("\u{FFFF}")", "\xEF\xBF\xBF"},
        {R"("\u{10000}")", "\xF0\x90\x80\x80"},
        {R"("\u{1F600}")", "\xF0\x9F\x98\x80"},
        {R"("\u{10FFFF}")", "\xF4\x8F\xBF\xBF"},
    }));
    CAPTURE(source);

    CHECK(string_value(source) == value);
}

TEST_CASE("L7.5 \\u{} goes beyond Unicode, up to 2^31 - 1", "[lexer][string]") {
    const auto [source, value] = GENERATE(values<EscapeCase>({
        {R"("\u{D800}")", "\xED\xA0\x80"},           // a surrogate
        {R"("\u{110000}")", "\xF4\x90\x80\x80"},     // above U+10FFFF
        {R"("\u{1FFFFF}")", "\xF7\xBF\xBF\xBF"},     // the largest 4-byte value
        {R"("\u{200000}")", "\xF8\x88\x80\x80\x80"}, // 5 bytes
        {R"("\u{3FFFFFF}")", "\xFB\xBF\xBF\xBF\xBF"},
        {R"("\u{4000000}")", "\xFC\x84\x80\x80\x80\x80"}, // 6 bytes
        {R"("\u{7FFFFFFF}")", "\xFD\xBF\xBF\xBF\xBF\xBF"},
        {R"("\u{000000007FFFFFFF}")", "\xFD\xBF\xBF\xBF\xBF\xBF"},
    }));
    CAPTURE(source);

    CHECK(string_value(source) == value);
}

TEST_CASE("L7.6 malformed \\u escapes", "[lexer][string]") {
    CHECK(error_message(R"("\u48")") == R"(input:1: missing '{' near '"\u4')");
    CHECK(error_message(R"("\u{}")") == R"(input:1: hexadecimal digit expected near '"\u{}')");
    CHECK(error_message(R"("\u{g}")") == R"(input:1: hexadecimal digit expected near '"\u{g')");
    CHECK(error_message(R"("\u{48")") == R"(input:1: missing '}' near '"\u{48"')");
    CHECK(error_message(R"("\u{48 }")") == R"(input:1: missing '}' near '"\u{48 ')");
    CHECK(error_message(R"("\u{80000000}")") ==
          R"(input:1: UTF-8 value too large near '"\u{80000000')");
    CHECK(error_message(R"("\u{7FFFFFFF0}")") ==
          R"(input:1: UTF-8 value too large near '"\u{7FFFFFFF0')");
}

TEST_CASE("L7.6 escapes mixed with text", "[lexer][string]") {
    CHECK(string_value(R"("caf\u{E9} \x26 \65\z
                           !")") == "caf\xC3\xA9 & A!");
}
