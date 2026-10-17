#include <cstdint>
#include <format>
#include <string>
#include <variant>

#include <lua2/debug.hpp>

#include "gc/heap.hpp"
#include "lexer/lexer.hpp"
#include "lexer/token.hpp"
#include "parser/ast_printer.hpp"
#include "parser/parser.hpp"
#include "text_format.hpp"
#include "vm/compiler.hpp"
#include "vm/proto.hpp"

namespace lua2 {

namespace {

// The value column for a token, or an empty string when it has none.
std::string value_text(const Token& token) {
    switch (token.kind) {
    case TokenKind::Name: return std::get<std::string>(token.value);
    case TokenKind::String:
    case TokenKind::Other: return quoted(std::get<std::string>(token.value));
    case TokenKind::Integer: return std::format("{}", std::get<std::int64_t>(token.value));
    case TokenKind::Float: return format_float(std::get<double>(token.value));
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

std::expected<std::string, std::string> ast_dot_dump(std::string_view source,
                                                     std::string_view chunk_name) {
    Parser parser{source, chunk_name};
    auto ast = parser.parse_chunk();
    if (!ast) {
        return std::unexpected(std::move(ast.error().message));
    }
    return print_ast_dot(*ast);
}

std::expected<std::string, std::string> bytecode_dump(std::string_view source,
                                                      std::string_view chunk_name) {
    Parser parser{source, chunk_name};
    auto ast = parser.parse_chunk();
    if (!ast) {
        return std::unexpected(std::move(ast.error().message));
    }
    // A heap of its own: the protos die with it. The chunk is named like a
    // file, as luac names it.
    Heap heap;
    const std::string file_source = "@" + std::string{chunk_name};
    auto proto = compile(heap, *ast, file_source, chunk_name);
    if (!proto) {
        return std::unexpected(std::move(proto.error().message));
    }
    return list_proto(**proto);
}

} // namespace lua2
