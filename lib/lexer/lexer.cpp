#include "lexer/lexer.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace lua2 {

namespace {

// Character classes (L1.3). These are plain ASCII on purpose: std::isalpha
// depends on the C locale, and bytes 128-255 must never be letters.
constexpr bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

constexpr bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

} // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

// @TODO: make more efficient
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

        // Tokens that are always one character (L8.1).
        // ('-' becomes a comment in part 5, '[' a long string in part 10.)
        case '+': return single(TokenKind::Plus);
        case '-': return single(TokenKind::Minus);
        case '*': return single(TokenKind::Star);
        case '%': return single(TokenKind::Percent);
        case '^': return single(TokenKind::Caret);
        case '#': return single(TokenKind::Hash);
        case '&': return single(TokenKind::Ampersand);
        case '|': return single(TokenKind::Pipe);
        case '(': return single(TokenKind::LParen);
        case ')': return single(TokenKind::RParen);
        case '{': return single(TokenKind::LBrace);
        case '}': return single(TokenKind::RBrace);
        case '[': return single(TokenKind::LBracket);
        case ']': return single(TokenKind::RBracket);
        case ';': return single(TokenKind::Semicolon);
        case ',': return single(TokenKind::Comma);

        // Tokens that may continue: peek ahead and take the longest one (L8.2).
        case '=': return peek(1) == '=' ? take(2, TokenKind::Equal) : single(TokenKind::Assign);
        case '~': return peek(1) == '=' ? take(2, TokenKind::NotEqual) : single(TokenKind::Tilde);
        case '/':
            return peek(1) == '/' ? take(2, TokenKind::DoubleSlash) : single(TokenKind::Slash);
        case ':':
            return peek(1) == ':' ? take(2, TokenKind::DoubleColon) : single(TokenKind::Colon);
        case '<':
            if (peek(1) == '=') {
                return take(2, TokenKind::LessEqual);
            }
            if (peek(1) == '<') {
                return take(2, TokenKind::ShiftLeft);
            }
            return single(TokenKind::Less);
        case '>':
            if (peek(1) == '=') {
                return take(2, TokenKind::GreaterEqual);
            }
            if (peek(1) == '>') {
                return take(2, TokenKind::ShiftRight);
            }
            return single(TokenKind::Greater);
        case '.':
            // (A digit after '.' starts a number, as in .5: that comes in part 6.)
            if (peek(1) == '.') {
                return peek(2) == '.' ? take(3, TokenKind::Ellipsis) : take(2, TokenKind::Concat);
            }
            return single(TokenKind::Dot);

        default:
            // A letter or '_' starts a name or a keyword (L4.1).
            if (is_letter(c)) {
                return read_name();
            }
            // Numbers, strings and the rest arrive in later parts.
            throw std::logic_error("lexer: unsupported character '" + std::string(1, c) + "'");
        }
    }
}

Token Lexer::make(TokenKind kind) const {
    return Token{.kind = kind, .line = line_, .value = {}};
}

Token Lexer::single(TokenKind kind) {
    return take(1, kind);
}

Token Lexer::take(std::size_t length, TokenKind kind) {
    // Build the token first so it gets the line it starts on.
    Token token = make(kind);
    pos_ += length;
    return token;
}

char Lexer::peek(std::size_t offset) const {
    const std::size_t index = pos_ + offset;
    return index < source_.size() ? source_[index] : '\0';
}

// @TODO: make more efficient
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

// @TODO: make more efficient
Token Lexer::read_name() {
    // Maximal munch (L4.4): take every letter and digit, so `whilex` is one
    // name rather than the keyword `while` followed by `x`.
    const std::size_t start = pos_;
    while (is_letter(peek(0)) || is_digit(peek(0))) {
        ++pos_;
    }
    const std::string_view text = source_.substr(start, pos_ - start);

    // A name is a keyword exactly when its text is in the table (L4.3).
    // The search compares `text` with each entry's Keyword::text.
    if (const auto* const keyword = std::ranges::find(keywords, text, &Keyword::text);
        keyword != keywords.end()) {
        return make(keyword->kind);
    }
    return Token{.kind = TokenKind::Name, .line = line_, .value = std::string(text)};
}

} // namespace lua2
