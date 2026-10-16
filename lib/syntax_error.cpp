#include "syntax_error.hpp"

#include <format>

namespace lua2 {

SyntaxError make_syntax_error(std::string_view chunk, int line, std::string_view message,
                              std::string_view near, std::source_location origin) {
    return SyntaxError{
        .message = std::format("{}:{}: {} near {}", chunk, line, message, near),
        .line = line,
        .origin = origin,
    };
}

std::string near_char(unsigned char c) {
    // Printable ASCII is space (32) through '~' (126). Like is_letter in the
    // lexer, this is a plain range check: std::isprint depends on the locale.
    if (c >= ' ' && c <= '~') {
        return std::format("'{}'", static_cast<char>(c));
    }
    return std::format("'<\\{}>'", c);
}

std::string near_text(std::string_view text) {
    return std::format("'{}'", text);
}

} // namespace lua2
