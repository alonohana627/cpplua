#include "lexer/lexer.hpp"

#include <stdexcept>

namespace lua2 {

Lexer::Lexer(std::string_view source) : source_(source) {}

Token Lexer::next() {
    if (pos_ >= source_.size()) {
        return Token{.kind = TokenKind::EndOfStream, .line = line_, .value = {}};
    }
    // Part 1 replaces this with the main loop: skip whitespace, look at
    // source_[pos_], and decide which token starts there.
    throw std::logic_error("lexer: only empty input is supported so far");
}

} // namespace lua2
