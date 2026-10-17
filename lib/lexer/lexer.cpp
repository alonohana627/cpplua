#include "lexer/lexer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "numeral.hpp"
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

// Whitespace for \z (L1.3): the same set next() skips, line breaks included.
constexpr bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\n' || c == '\r';
}

// Appends `code` encoded as UTF-8 (L7.5). This is the original UTF-8 scheme,
// as in Lua's luaO_utf8esc: up to 6 bytes, so it covers every value below
// 2^31, including surrogates and values above U+10FFFF.
void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code); // ASCII: one byte
        return;
    }
    // Fill continuation bytes (10xxxxxx) from the end. `first_max` is the
    // largest value that still fits in the first byte; each continuation
    // byte leaves one bit less room there.
    std::array<char, 6> bytes{};
    std::size_t count = 0;
    std::uint32_t first_max = 0x3F;
    do {
        bytes.at(5 - count) = static_cast<char>(0x80U | (code & 0x3FU));
        ++count;
        code >>= 6U;
        first_max >>= 1U;
    } while (code > first_max);
    // The first byte: as many leading 1 bits as there are bytes, then a 0,
    // then what is left of the code.
    bytes.at(5 - count) = static_cast<char>(((~first_max << 1U) | code) & 0xFFU);
    ++count;
    out.append(bytes.data() + (6 - count), count);
}

} // namespace

Lexer::Lexer(std::string_view source, std::string_view chunk_name)
    : source_(source), chunk_name_(chunk_name) {}

std::expected<Token, SyntaxError> Lexer::next() {
    // One error at a time: after an error, lexing is over.
    if (failed_) {
        return std::unexpected(*failed_);
    }
    // Token offsets are 32 bits (Token::offset), so refuse sources that big.
    if (source_.size() > std::numeric_limits<std::uint32_t>::max()) {
        return error("chunk too large", near_eof);
    }
    auto token = scan();
    if (token) {
        // Record where the token is in the source. scan() set token_start_
        // after skipping whitespace and comments, and pos_ is now its end.
        token->offset = static_cast<std::uint32_t>(token_start_);
        token->length = static_cast<std::uint32_t>(pos_ - token_start_);
        token->end_line = line_; // past a multi-line string, its last line
    }
    return token;
}

// @TODO: make more efficient
std::expected<Token, SyntaxError> Lexer::scan() {
    for (;;) {
        token_start_ = pos_;
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

        // '-' is minus, unless a second '-' starts a comment (L3.2, L8.3).
        case '-':
            if (peek(1) != '-') {
                return single(TokenKind::Minus);
            }
            if (auto skipped = skip_comment(); !skipped) {
                return std::unexpected(std::move(skipped.error()));
            }
            continue;

        // Quotes start a short string (L7.1).
        case '"':
        case '\'': return read_string();

        // '[' is a bracket, unless it opens a long string: [[ or [==[ (L6).
        case '[': {
            const LongBracket bracket = long_bracket();
            if (bracket.valid) {
                return read_long_string(bracket.level);
            }
            if (bracket.level == 0) {
                return single(TokenKind::LBracket); // L6.3
            }
            // '[' and '=' signs, but no second '[' (L6.2). Like Lua, the
            // near text is what was read: '[=='.
            const std::string_view read = source_.substr(pos_, 1 + bracket.level);
            pos_ += read.size();
            return error("invalid long string delimiter", near_text(read));
        }

        // Tokens that are always one character (L8.1).
        case '+': return single(TokenKind::Plus);
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
    // Step 1: read greedily, exactly like Lua's llex.c (L5.2). Take every
    // character that could belong to a numeral, without checking the shape
    // yet. The caller has checked that we start at a digit, or at '.'
    // before a digit.
    const std::size_t start = pos_;
    char exponent_lower = 'e'; // decimal exponents are e/E
    char exponent_upper = 'E';
    if (peek(0) == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        pos_ += 2;
        exponent_lower = 'p'; // hex exponents are p/P
        exponent_upper = 'P';
    }
    for (;;) {
        const char c = peek(0);
        if (c == exponent_lower || c == exponent_upper) {
            ++pos_; // the exponent letter, then an optional sign
            if (peek(0) == '+' || peek(0) == '-') {
                ++pos_;
            }
        } else if (is_hex_digit(c) || c == '.') {
            ++pos_; // hex digits even in a decimal numeral: `3a` is read whole
        } else {
            break;
        }
    }
    // A letter touching the numeral is read too, so that `3x` and `0xg` fail
    // as one malformed number instead of splitting into a number and a name.
    if (is_letter(peek(0))) {
        ++pos_;
    }
    const std::string_view text = source_.substr(start, pos_ - start);

    // Step 2: validate and convert. An integer first (L5.6-L5.8), then a
    // float (L5.6, L5.9); if neither accepts the whole text, it is malformed.
    if (const auto integer = integer_numeral(text)) {
        return Token{.kind = TokenKind::Integer, .line = line_, .value = *integer};
    }
    if (const auto number = float_numeral(text)) {
        return Token{.kind = TokenKind::Float, .line = line_, .value = *number};
    }
    return error("malformed number", near_text(text));
}

std::expected<void, SyntaxError> Lexer::skip_comment() {
    pos_ += 2; // the "--"

    // A long bracket right after "--" makes a long comment (L3.3).
    if (peek(0) == '[') {
        if (const LongBracket bracket = long_bracket(); bracket.valid) {
            if (auto comment = read_long_bracket(bracket.level, LongKind::Comment); !comment) {
                return std::unexpected(std::move(comment.error()));
            }
            return {};
        }
    }

    // Anything else, including "--[" and "--[=x", is a short comment (L3.4).
    // It runs to the end of the line. The line break is not part of it:
    // next() reads it afterwards and counts the line. A comment may also end
    // at the end of the input (L3.6).
    while (pos_ < source_.size() && peek(0) != '\n' && peek(0) != '\r') {
        ++pos_;
    }
    return {};
}

Lexer::LongBracket Lexer::long_bracket() const {
    // Count the '=' signs after the bracket, then check that the same
    // bracket follows: [==[ or ]==] (L6.1). Nothing is consumed.
    const char bracket = peek(0);
    std::size_t level = 0;
    while (peek(1 + level) == '=') {
        ++level;
    }
    return LongBracket{.level = level, .valid = peek(1 + level) == bracket};
}

std::expected<Token, SyntaxError> Lexer::read_long_string(std::size_t level) {
    const int start_line = line_; // the token gets the line it starts on (L2.2)
    auto text = read_long_bracket(level, LongKind::String);
    if (!text) {
        return std::unexpected(std::move(text.error()));
    }
    return Token{.kind = TokenKind::String, .line = start_line, .value = std::move(*text)};
}

std::expected<std::string, SyntaxError> Lexer::read_long_bracket(std::size_t level, LongKind kind) {
    const int start_line = line_;
    pos_ += level + 2; // the opening bracket: '[', the '=' signs, '['

    // A line break right after the opening bracket is dropped (L7.11).
    if (peek(0) == '\n' || peek(0) == '\r') {
        read_newline();
    }

    std::string text; // stays empty for a comment
    for (;;) {
        if (pos_ >= source_.size()) {
            // L7.12, L3.5. The message names the line the bracket opened on.
            const std::string message =
                std::format("unfinished long {} (starting at line {})",
                            kind == LongKind::String ? "string" : "comment", start_line);
            return error(message, near_eof);
        }
        const char c = source_[pos_];
        switch (c) {
        case ']':
            // The first closing bracket of the *same* level ends it (L7.8).
            // A ']' of another level is just text.
            if (const LongBracket bracket = long_bracket();
                bracket.valid && bracket.level == level) {
                pos_ += level + 2;
                return text;
            }
            break;

        // Every line break becomes a single '\n' (L7.10), and is counted.
        case '\n':
        case '\r':
            read_newline();
            if (kind == LongKind::String) {
                text += '\n';
            }
            continue;

        default: break;
        }
        // Any other byte stands for itself. No escapes inside (L7.9).
        if (kind == LongKind::String) {
            text += c;
        }
        ++pos_;
    }
}

std::expected<Token, SyntaxError> Lexer::read_string() {
    const char quote = peek(0);
    const int start_line = line_; // the token gets the line it starts on (L2.2)
    ++pos_;

    // The string's bytes, after escapes are processed. Like Lua, it starts
    // with the opening quote, because error messages show the string read so
    // far: unfinished string near '"abc'. The quote is dropped at the end.
    std::string text(1, quote);

    for (;;) {
        if (pos_ >= source_.size()) {
            return error("unfinished string", near_eof); // L7.2
        }
        const char c = source_[pos_];
        if (c == quote) {
            ++pos_;
            break;
        }
        switch (c) {
        // A raw line break cannot appear in a short string (L7.2).
        case '\n':
        case '\r': return error("unfinished string", near_text(text));

        case '\\':
            if (auto escaped = read_escape(text); !escaped) {
                return std::unexpected(std::move(escaped.error()));
            }
            break;

        // Any other byte stands for itself (L7.7), including '\0' and
        // bytes above 127.
        default:
            text += c;
            ++pos_;
            break;
        }
    }

    text.erase(0, 1); // drop the opening quote
    return Token{.kind = TokenKind::String, .line = start_line, .value = std::move(text)};
}

std::expected<void, SyntaxError> Lexer::read_escape(std::string& text) {
    ++pos_; // the backslash
    if (pos_ >= source_.size()) {
        return {}; // end of input: read_string reports the unfinished string
    }
    const char escape = source_[pos_];
    switch (escape) {
    // Named escapes (L7.3).
    case 'a': text += '\a'; break;
    case 'b': text += '\b'; break;
    case 'f': text += '\f'; break;
    case 'n': text += '\n'; break;
    case 'r': text += '\r'; break;
    case 't': text += '\t'; break;
    case 'v': text += '\v'; break;
    case '\\':
    case '"':
    case '\'': text += escape; break;

    // A backslash before a real line break is a newline, whatever the line
    // break was, and the line is counted (L7.3).
    case '\n':
    case '\r':
        read_newline();
        text += '\n';
        return {}; // read_newline already moved past it

    // \xXX: exactly two hex digits (L7.3).
    case 'x': {
        std::string raw = "\\x"; // what was read, for error messages
        ++pos_;
        unsigned value = 0;
        for (int i = 0; i < 2; ++i) {
            if (!is_hex_digit(peek(0))) {
                return escape_error("hexadecimal digit expected", text, raw);
            }
            value = (value * 16) + hex_value(peek(0));
            raw += peek(0);
            ++pos_;
        }
        text += static_cast<char>(value);
        return {};
    }

    // \z: skip all following whitespace, line breaks included (L7.3).
    case 'z':
        ++pos_;
        while (is_space(peek(0))) { // peek gives '\0' at the end, which is not a space
            if (peek(0) == '\n' || peek(0) == '\r') {
                read_newline();
            } else {
                ++pos_;
            }
        }
        return {};

    // \u{XXX}: a code point below 2^31, written as UTF-8 (L7.5).
    case 'u': {
        // @TODO: should be in a seperate function called handle_utf8
        std::string raw = "\\u";
        ++pos_;
        if (peek(0) != '{') {
            return escape_error("missing '{'", text, raw);
        }
        raw += '{';
        ++pos_;
        if (!is_hex_digit(peek(0))) {
            return escape_error("hexadecimal digit expected", text, raw);
        }
        std::uint32_t code = 0;
        while (is_hex_digit(peek(0))) {
            // One more hex digit multiplies by 16: refuse before passing 2^31 - 1.
            if (code > (0x7FFF'FFFFU >> 4U)) {
                return escape_error("UTF-8 value too large", text, raw);
            }
            code = (code * 16) + hex_value(peek(0));
            raw += peek(0);
            ++pos_;
        }
        if (peek(0) != '}') {
            return escape_error("missing '}'", text, raw);
        }
        ++pos_;
        append_utf8(text, code);
        return {};
    }

    default:
        // \ddd: one to three decimal digits, at most 255 (L7.3, L7.4).
        if (is_digit(escape)) {
            std::string raw = "\\";
            unsigned value = 0;
            for (int i = 0; i < 3 && is_digit(peek(0)); ++i) {
                value = (value * 10) + static_cast<unsigned>(peek(0) - '0');
                raw += peek(0);
                ++pos_;
            }
            if (value > 255) {
                return escape_error("decimal escape too large", text, raw);
            }
            text += static_cast<char>(value);
            return {};
        }
        // Anything else is an invalid escape (L7.6).
        return escape_error("invalid escape sequence", text, "\\");
    }
    ++pos_; // the named escape's character
    return {};
}

std::unexpected<SyntaxError> Lexer::escape_error(std::string_view message, std::string_view text,
                                                 std::string_view raw,
                                                 std::source_location origin) {
    // Like Lua, show the string so far, the escape as written so far, and the
    // character that broke it (unless the input ended): '"abc\x4"'.
    std::string near{text};
    near += raw;
    if (pos_ < source_.size()) {
        near += source_[pos_];
    }
    return error(message, near_text(near), origin);
}

} // namespace lua2
