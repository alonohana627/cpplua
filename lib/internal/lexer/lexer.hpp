#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "lexer/token.hpp"
#include "syntax_error.hpp"

namespace lua2 {

// Turns Lua source text into tokens, one at a time.
//
// The lexer does not copy the source: it only keeps a view of it, so the
// caller must keep the text alive while lexing. [[clang::lifetimebound]] asks
// clang to warn when it can see that the text dies first, as in
// `Lexer lexer{std::string("x = 1")};`.
class Lexer {
public:
    // `chunk_name` is the name errors start with, as in `input:3: ...` (L10.1).
    // Lua uses "?" when a chunk has no name. Like the source, it is not copied.
    explicit Lexer(std::string_view source [[clang::lifetimebound]],
                   std::string_view chunk_name [[clang::lifetimebound]] = "?");

    // Returns the next token, or the SyntaxError that stopped lexing.
    //
    // At the end of the input it returns EndOfStream, and keeps returning it
    // on every later call (L1.2). After an error, lexing is over: every later
    // call returns that same error. Nothing is thrown.
    [[nodiscard]] std::expected<Token, SyntaxError> next();

    [[nodiscard]] std::string_view chunk_name() const noexcept { return chunk_name_; }

private:
    // Records a SyntaxError on the current line, such as
    // `input:3: unfinished string near '"abc'` (L10.1), and returns it ready
    // for `return error(...);`. `origin` is where in our code it was called.
    std::unexpected<SyntaxError>
    error(std::string_view message, std::string_view near,
          std::source_location origin = std::source_location::current());

    // Builds a token of `kind` on the current line, with no value.
    [[nodiscard]] Token make(TokenKind kind) const;

    // Consumes one character and returns a token of `kind` for it.
    Token single(TokenKind kind);

    // Consumes `length` characters and returns a token of `kind` for them.
    Token take(std::size_t length, TokenKind kind);

    // Looks `offset` characters ahead without consuming anything.
    // Returns '\0' past the end of the input.
    [[nodiscard]] char peek(std::size_t offset) const;

    // Consumes one line break (\n, \r, \r\n or \n\r) and counts the line (L2.1).
    void read_newline();

    // Consumes a name and returns it as a Name token, or as a keyword token
    // when its text is one of the 22 keywords (L4.1-L4.4).
    Token read_name();

    // Consumes a decimal or hex numeral and returns it as an Integer or Float
    // token, or a "malformed number" error (L5).
    std::expected<Token, SyntaxError> read_number();

    // Consumes a comment starting at "--": a long comment when a long
    // bracket follows (L3.3), otherwise up to the end of the line (L3.4).
    // Only an unfinished long comment is an error (L3.5).
    std::expected<void, SyntaxError> skip_comment();

    // A long bracket at the current '[' or ']' (L6.1): its level (the
    // number of '=' signs), and whether the same bracket follows them.
    struct LongBracket {
        std::size_t level = 0;
        bool valid = false;
    };

    // Looks at the bracket under the cursor without consuming anything.
    [[nodiscard]] LongBracket long_bracket() const;

    // Whether a long bracket holds a string or a comment. They are read the
    // same way; a comment keeps no text and says "comment" in its error.
    enum class LongKind : std::uint8_t { String, Comment };

    // Consumes a long string [==[ ... ]==] of `level` and returns it as a
    // String token (L7.8-L7.12).
    std::expected<Token, SyntaxError> read_long_string(std::size_t level);

    // Consumes a long bracket of `level`, from its opening bracket to the
    // matching closing one, and returns the text in between (L6, L7.8-L7.12).
    std::expected<std::string, SyntaxError> read_long_bracket(std::size_t level, LongKind kind);

    // Consumes a short string in '...' or "..." and returns it as a String
    // token holding its bytes after escapes, or an error for an unfinished
    // string or a bad escape (L7.1-L7.3, L7.6, L7.7).
    std::expected<Token, SyntaxError> read_string();

    // Consumes one escape sequence, starting at its backslash, and appends
    // the bytes it stands for to `text` (L7.3-L7.6).
    std::expected<void, SyntaxError> read_escape(std::string& text);

    // An error inside an escape. Like Lua, the near text is the string so far
    // (`text`), the escape as written so far (`raw`), and the offending
    // character, as in `'"abc\x4"'`.
    std::unexpected<SyntaxError>
    escape_error(std::string_view message, std::string_view text, std::string_view raw,
                 std::source_location origin = std::source_location::current());

    std::string_view source_;
    std::string_view chunk_name_;
    std::size_t pos_ = 0; // index of the next character to read
    int line_ = 1;        // current line (L2.2)

    // The error that stopped lexing, if any. Once set, next() returns it.
    std::optional<SyntaxError> failed_;
};

} // namespace lua2
