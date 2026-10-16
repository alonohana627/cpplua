#include "lexer/lexer.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>

#include "syntax_error.hpp"

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

Lexer::Lexer(std::string_view source, std::string_view chunk_name)
    : source_(source), chunk_name_(chunk_name) {}

// @TODO: make more efficient
std::expected<Token, SyntaxError> Lexer::next() {
    // One error at a time: after an error, lexing is over.
    if (failed_) {
        return std::unexpected(*failed_);
    }
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
            if (peek(1) == '.') {
                return peek(2) == '.' ? take(3, TokenKind::Ellipsis) : take(2, TokenKind::Concat);
            }
            // A digit after '.' starts a number, as in .5 (L5.1, L8.4).
            if (is_digit(peek(1))) {
                return read_number();
            }
            return single(TokenKind::Dot);

        default:
            // A letter or '_' starts a name or a keyword (L4.1).
            if (is_letter(c)) {
                return read_name();
            }
            // A digit starts a number (L5.1).
            if (is_digit(c)) {
                return read_number();
            }
            // Strings arrive in part 8.
            if (c == '"' || c == '\'') {
                return error("not supported yet", near_char(static_cast<unsigned char>(c)));
            }
            // Any other byte, such as @, $ or a byte above 127, is not an
            // error here: it becomes an Other token, and the parser rejects
            // it with "unexpected symbol near '@'" (L9.1).
            Token token = make(TokenKind::Other);
            token.value = std::string(1, c);
            ++pos_;
            return token;
        }
    }
}

std::unexpected<SyntaxError> Lexer::error(std::string_view message, std::string_view near,
                                          std::source_location origin) {
    failed_ = make_syntax_error(chunk_name_, line_, message, near, origin);
    return std::unexpected(*failed_);
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

// @TODO: make more efficient
std::expected<Token, SyntaxError> Lexer::read_number() {
    // Step 1: find where the numeral ends (L5.4):
    //   digits [ '.' digits ] [ ('e' | 'E') [ '+' | '-' ] digits ]
    // with at least one digit before or after the '.'. The caller has
    // already checked that we start at a digit, or at '.' before a digit.
    const std::size_t start = pos_;
    bool is_float = false;

    while (is_digit(peek(0))) {
        ++pos_;
    }
    if (peek(0) == '.') {
        is_float = true;
        ++pos_;
        while (is_digit(peek(0))) {
            ++pos_;
        }
    }
    if (peek(0) == 'e' || peek(0) == 'E') {
        is_float = true;
        ++pos_;
        if (peek(0) == '+' || peek(0) == '-') {
            ++pos_;
        }
        // The exponent needs at least one digit: `1e` and `1e+` are errors.
        if (!is_digit(peek(0))) {
            return error("malformed number", near_text(source_.substr(start, pos_ - start)));
        }
        while (is_digit(peek(0))) {
            ++pos_;
        }
    }
    const std::string_view text = source_.substr(start, pos_ - start);
    const char* const first = text.data();
    const char* const last = text.data() + text.size();

    // Step 2: convert the text. std::from_chars never depends on the locale,
    // never allocates, and reports problems through an error code.

    // No '.' and no exponent: an integer, if it fits in 64 bits (L5.6, L5.7).
    if (!is_float) {
        std::int64_t integer = 0;
        if (std::from_chars(first, last, integer).ec == std::errc{}) {
            return Token{.kind = TokenKind::Integer, .line = line_, .value = integer};
        }
        // Too large for an integer: Lua reads it as a float instead (L5.7).
    }

    // A float (L5.6, L5.9). Too large or too small is not an error in Lua:
    // 1e400 is infinity and 1e-400 is 0. from_chars reports those as
    // errc::result_out_of_range, and libc++ still stores inf or 0 in `number`
    // (the tests pin this down), so that code is accepted too.
    double number = 0.0;
    const auto [end, ec] = std::from_chars(first, last, number);
    if ((ec != std::errc{} && ec != std::errc::result_out_of_range) || end != last) {
        // Step 1 only accepts well-formed numerals, so this cannot happen.
        return error("malformed number", near_text(text));
    }
    return Token{.kind = TokenKind::Float, .line = line_, .value = number};
}

} // namespace lua2
