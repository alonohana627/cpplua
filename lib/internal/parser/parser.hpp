#pragma once

#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "lexer/lexer.hpp"
#include "lexer/token.hpp"
#include "parser/ast.hpp"
#include "syntax_error.hpp"

namespace lua2 {

// Turns Lua source text into an Ast (docs/spec/grammar.md).
//
// A recursive-descent parser, like Lua's lparser.c: one function per grammar
// rule, reading tokens from the Lexer one at a time. Like the lexer, it
// returns errors instead of throwing, and stops at the first one (G9.4).
// The source must outlive the parser.
class Parser {
public:
    explicit Parser(std::string_view source [[clang::lifetimebound]],
                    std::string_view chunk_name [[clang::lifetimebound]] = "?");

    // Parses the whole chunk (G2.1). Call it once.
    [[nodiscard]] std::expected<Ast, SyntaxError> parse_chunk();

private:
    // ---- Grammar rules ----

    // block ::= { stat } [ retstat ] (G2.2)
    std::expected<Block, SyntaxError> parse_block();

    // retstat ::= 'return' [ explist ] [ ';' ] (G2.3)
    std::expected<StatId, SyntaxError> parse_return();

    // explist ::= exp { ',' exp } (G4.6)
    std::expected<IdRange<ExprId>, SyntaxError> parse_expression_list();

    // exp (G4.1). For now only the simple expressions: nil, true, false,
    // numerals, strings and '...'.
    std::expected<ExprId, SyntaxError> parse_expression();

    // Whether the current token ends a block: <eof>, 'end', 'else',
    // 'elseif', and 'until' (which ends a repeat block).
    [[nodiscard]] bool block_follow() const noexcept;

    // ---- Moving through tokens ----

    // Moves to the next token: the lookahead token if peek() read one,
    // otherwise a new one from the lexer.
    std::expected<void, SyntaxError> advance();

    // The token after the current one, without moving. Lua's grammar needs
    // this in one place only: table fields, {x = 1} versus {x} (G6.5).
    std::expected<const Token*, SyntaxError> peek();

    // Whether the current token is of `kind`.
    [[nodiscard]] bool check(TokenKind kind) const noexcept { return current_.kind == kind; }

    // If the current token is of `kind`, moves past it and returns true.
    // Otherwise stays and returns false.
    std::expected<bool, SyntaxError> accept(TokenKind kind);

    // The current token must be of `kind`: moves past it, or returns the
    // error "'X' expected near ..." (G9.3).
    std::expected<void, SyntaxError> expect(TokenKind kind);

    // ---- Errors ----

    // An error at the current token: "<chunk>:<line>: <message> near <token>".
    [[nodiscard]] std::unexpected<SyntaxError>
    error(std::string_view message,
          std::source_location origin = std::source_location::current()) const;

    // "'X' expected near ..." (G9.3).
    [[nodiscard]] std::unexpected<SyntaxError>
    error_expected(TokenKind kind,
                   std::source_location origin = std::source_location::current()) const;

    // How errors show the current token (G9.2): as written in the source,
    // quoted, or <eof>.
    [[nodiscard]] std::string near_current() const;

    std::string_view source_;
    Lexer lexer_;
    Token current_;                  // the token being looked at
    std::optional<Token> lookahead_; // the next one, once peek() has read it
    Ast ast_;
};

} // namespace lua2
