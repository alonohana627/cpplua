#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace lua2 {

// Every kind of token the lexer can produce. Later parts add names, numbers,
// strings, keywords and operators (see docs/spec/lexical.md, L11).
enum class TokenKind : std::uint8_t {
    EndOfStream,
};

// What a token carries besides its kind:
//   std::monostate  nothing (operators, keywords, EndOfStream)
//   std::string     a name's text or a string's bytes
//   std::int64_t    an integer numeral
//   double          a float numeral
using TokenValue = std::variant<std::monostate, std::string, std::int64_t, double>;

struct Token {
    TokenKind kind = TokenKind::EndOfStream;
    int line = 1; // the line the token starts on, counted from 1 (L2.2)
    TokenValue value;

    bool operator==(const Token&) const = default;
};

} // namespace lua2
