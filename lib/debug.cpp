#include <cstdint>
#include <format>
#include <string>
#include <variant>

#include <lua2/debug.hpp>

#include "lexer/lexer.hpp"
#include "lexer/token.hpp"
#include "parser/ast_printer.hpp"
#include "parser/parser.hpp"

namespace lua2 {

namespace {

// A string's bytes in double quotes, with anything unprintable escaped, so
// every token fits on one line: "a\n\x01".
std::string quoted(std::string_view bytes) {
    std::string out = "\"";
    for (const char c : bytes) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c >= ' ' && c <= '~') {
                out += c;
            } else {
                out += std::format("\\x{:02X}", static_cast<unsigned char>(c));
            }
            break;
        }
    }
    out += '"';
    return out;
}

// The value column for a token, or an empty string when it has none.
std::string value_text(const Token& token) {
    switch (token.kind) {
    case TokenKind::Name: return std::get<std::string>(token.value);
    case TokenKind::String:
    case TokenKind::Other: return quoted(std::get<std::string>(token.value));
    case TokenKind::Integer: return std::format("{}", std::get<std::int64_t>(token.value));
    case TokenKind::Float: {
        // Shortest form that reads back exactly. Like Lua, add ".0" when it
        // would look like an integer, so 1e3 shows as 1000.0, not 1000.
        std::string text = std::format("{}", std::get<double>(token.value));
        if (text.find_first_of(".eni") == std::string::npos) {
            text += ".0";
        }
        return text;
    }
    default: return {};
    }
}

} // namespace

std::expected<std::string, std::string> token_dump(std::string_view source,
                                                   std::string_view chunk_name) {
    Lexer lexer{source, chunk_name};
    std::string out;
    for (;;) {
        auto token = lexer.next();
        if (!token) {
            return std::unexpected(std::move(token.error().message));
        }
        const std::string value = value_text(*token);
        out += std::format("{:<5} {:<12} {}", token->line, token_kind_name(token->kind), value);
        // No trailing spaces on lines without a value.
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
        out += '\n';
        if (token->kind == TokenKind::EndOfStream) {
            return out;
        }
    }
}

std::expected<std::string, std::string> ast_dump(std::string_view source,
                                                 std::string_view chunk_name) {
    Parser parser{source, chunk_name};
    auto ast = parser.parse_chunk();
    if (!ast) {
        return std::unexpected(std::move(ast.error().message));
    }
    return print_ast(*ast);
}

} // namespace lua2
