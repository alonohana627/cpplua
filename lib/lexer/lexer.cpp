#include "lexer/lexer.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

constexpr bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// The value of a hex digit; only call it when is_hex_digit(c) is true.
constexpr unsigned hex_value(char c) {
    if (is_digit(c)) {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a' + 10);
    }
    return static_cast<unsigned>(c - 'A' + 10);
}

constexpr bool has_hex_prefix(std::string_view text) {
    return text.starts_with("0x") || text.starts_with("0X");
}

// Converts a whole numeral to an integer, or returns nothing when the text is
// not an integer numeral (it has a point or an exponent, or is malformed) or
// is a decimal too large for 64 bits.
std::optional<std::int64_t> to_integer(std::string_view text) {
    if (has_hex_prefix(text)) {
        // Hex: at least one hex digit, and nothing else (L5.5). The value
        // wraps around modulo 2^64 (L5.8): unsigned arithmetic wraps by
        // definition, and the conversion to signed keeps the bits.
        const std::string_view digits = text.substr(2);
        if (digits.empty() || !std::ranges::all_of(digits, is_hex_digit)) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (const char c : digits) {
            value = (value * 16) + hex_value(c);
        }
        return static_cast<std::int64_t>(value);
    }
    // Decimal: from_chars must use the whole text. On overflow it reports
    // errc::result_out_of_range, and the numeral becomes a float (L5.7).
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// Converts a whole numeral to a float, or returns nothing when it is
// malformed. Hex floats use from_chars' hex format, which expects the digits
// without the 0x prefix.
std::optional<double> to_float(std::string_view text) {
    const bool hex = has_hex_prefix(text);
    const std::string_view digits = hex ? text.substr(2) : text;
    const auto format = hex ? std::chars_format::hex : std::chars_format::general;

    // Too large or too small is not an error in Lua (L5.9): 1e400 is
    // infinity and 1e-400 is 0. from_chars reports those as
    // errc::result_out_of_range, and libc++ still stores inf or 0 in `value`
    // (the tests pin this down), so that code is accepted too.
    double value = 0.0;
    const auto [end, ec] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value, format);
    if ((ec != std::errc{} && ec != std::errc::result_out_of_range) ||
        end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
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

        // '-' is minus, unless a second '-' starts a comment (L3.2, L8.3).
        case '-':
            if (peek(1) != '-') {
                return single(TokenKind::Minus);
            }
            skip_comment();
            continue;

        // Quotes start a short string (L7.1).
        case '"':
        case '\'': return read_string();

        // Tokens that are always one character (L8.1).
        // ('[' becomes a long string in part 10.)
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
    if (const auto integer = to_integer(text)) {
        return Token{.kind = TokenKind::Integer, .line = line_, .value = *integer};
    }
    if (const auto number = to_float(text)) {
        return Token{.kind = TokenKind::Float, .line = line_, .value = *number};
    }
    return error("malformed number", near_text(text));
}

void Lexer::skip_comment() {
    // A short comment runs to the end of the line (L3.4). The line break is
    // not part of it: next() reads it afterwards and counts the line. A
    // comment may also end at the end of the input (L3.6).
    // (Long comments, --[[ ... ]], arrive with long brackets in part 10.
    // Until then they are read as short comments.)
    pos_ += 2; // the "--"
    while (pos_ < source_.size() && peek(0) != '\n' && peek(0) != '\r') {
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

        case '\\': {
            ++pos_; // the backslash
            if (pos_ >= source_.size()) {
                continue; // end of input: reported as unfinished above
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

            // A backslash before a real line break is a newline, whatever the
            // line break was, and the line is counted (L7.3).
            case '\n':
            case '\r':
                read_newline();
                text += '\n';
                continue; // read_newline already moved past it

            // \x, \z, \ddd and \u{} arrive in part 9. Anything else is an
            // error, shown with the backslash and the bad character (L7.6).
            default:
                text += '\\';
                text += escape;
                ++pos_;
                return error("invalid escape sequence", near_text(text));
            }
            ++pos_; // the escape character
            break;
        }

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

} // namespace lua2
