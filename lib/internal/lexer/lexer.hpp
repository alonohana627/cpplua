#pragma once

#include <cstddef>
#include <string_view>

#include "lexer/token.hpp"

namespace lua2 {

// Turns Lua source text into tokens, one at a time.
//
// The lexer does not copy the source: it only keeps a view of it, so the
// caller must keep the text alive while lexing. [[clang::lifetimebound]] asks
// clang to warn when it can see that the text dies first, as in
// `Lexer lexer{std::string("x = 1")};`.
class Lexer {
public:
    explicit Lexer(std::string_view source [[clang::lifetimebound]]);

    // Returns the next token. At the end of the input it returns
    // EndOfStream, and keeps returning it on every later call (L1.2).
    Token next();

private:
    // Builds a token of `kind` on the current line, with no value.
    [[nodiscard]] Token make(TokenKind kind) const;

    // Consumes one character and returns a token of `kind` for it.
    Token single(TokenKind kind);

    // Consumes one line break (\n, \r, \r\n or \n\r) and counts the line (L2.1).
    void read_newline();

    std::string_view source_;
    std::size_t pos_ = 0; // index of the next character to read
    int line_ = 1;        // current line (L2.2)
};

} // namespace lua2
