#include "lexer/lexer.hpp"

#include <stdexcept>
#include <string>

namespace lua2 {

Lexer::Lexer(std::string_view source) : source_(source) {}

Token Lexer::next() {
    for (;;) {
        if (pos_ >= source_.size()) {
            return make(TokenKind::EndOfStream);
        }
        const char c = source_[pos_];
        switch (c) {
        // Whitespace (L1.3, L3.1): skip it and look again.
        case ' ':
        case '\t':
        case '\v':
        case '\f': ++pos_; continue;

        // Line breaks (L2.1): skip them too, but count the line.
        case '\n':
        case '\r': read_newline(); continue;

        // Single-character tokens (L8.1).
        case '+': return single(TokenKind::Plus);
        case '-': return single(TokenKind::Minus);
        case '*': return single(TokenKind::Star);
        case '/': return single(TokenKind::Slash);
        case '%': return single(TokenKind::Percent);
        case '^': return single(TokenKind::Caret);
        case '#': return single(TokenKind::Hash);
        case '&': return single(TokenKind::Ampersand);
        case '~': return single(TokenKind::Tilde);
        case '|': return single(TokenKind::Pipe);
        case '<': return single(TokenKind::Less);
        case '>': return single(TokenKind::Greater);
        case '=': return single(TokenKind::Assign);
        case '(': return single(TokenKind::LParen);
        case ')': return single(TokenKind::RParen);
        case '{': return single(TokenKind::LBrace);
        case '}': return single(TokenKind::RBrace);
        case '[': return single(TokenKind::LBracket);
        case ']': return single(TokenKind::RBracket);
        case ';': return single(TokenKind::Semicolon);
        case ':': return single(TokenKind::Colon);
        case ',': return single(TokenKind::Comma);
        case '.': return single(TokenKind::Dot);

        default:
            // Names, numbers, strings and the rest arrive in later parts.
            throw std::logic_error("lexer: unsupported character '" + std::string(1, c) + "'");
        }
    }
}

Token Lexer::make(TokenKind kind) const {
    return Token{.kind = kind, .line = line_, .value = {}};
}

Token Lexer::single(TokenKind kind) {
    // Build the token first so it gets the line it starts on.
    Token token = make(kind);
    ++pos_;
    return token;
}

void Lexer::read_newline() {
    const char first = source_[pos_];
    ++pos_;
    // \r\n and \n\r are one line break, but \n\n and \r\r are two.
    if (pos_ < source_.size()) {
        const char second = source_[pos_];
        if ((second == '\n' || second == '\r') && second != first) {
            ++pos_;
        }
    }
    ++line_;
}

} // namespace lua2
