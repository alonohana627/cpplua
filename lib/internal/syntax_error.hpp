#pragma once

#include <source_location>
#include <string>
#include <string_view>

namespace cpplua {

// A lexical or syntax error. It is returned (inside std::expected), never
// thrown. The lexer produces it now, and the parser will too, which is why it
// lives outside lexer/.
struct SyntaxError {
    // The message Lua prints (L10.1): `<chunk>:<line>: <message> near <near>`,
    // for example `input:1: malformed number near '3x'`.
    std::string message;

    // The line the error was found on.
    int line = 0;

    // Where in *our* C++ code the error was created, for debugging the
    // interpreter itself. Not part of what Lua users see.
    std::source_location origin;
};

// Builds a SyntaxError. `near` is already formatted: a quoted token such as
// `'@'`, or `<eof>`. An empty `near` leaves the " near ..." part out, as Lua
// does for semantic errors (G9.5). `origin` defaults to the caller's location.
SyntaxError make_syntax_error(std::string_view chunk, int line, std::string_view message,
                              std::string_view near,
                              std::source_location origin = std::source_location::current());

// The `near` text for end of input (L10.2).
inline constexpr std::string_view near_eof = "<eof>";

// The `near` text for a single byte (L10.2): the character in quotes when it
// is printable ASCII, as in `'@'`, otherwise its decimal value, as in `'<\128>'`.
std::string near_char(unsigned char c);

// The `near` text for a name, number or string (L10.2): the text read so far,
// in quotes, as in `'1e+'`.
std::string near_text(std::string_view text);

} // namespace cpplua
