#pragma once

#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

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

    // stat (G3.1). Returns no id for an empty statement ';', which leaves
    // nothing in the tree.
    std::expected<std::optional<StatId>, SyntaxError> parse_statement();

    // A statement starting with an expression: a call, or an assignment
    // `varlist = explist`. Anything else is "syntax error" (G3.8, G3.9).
    // Lua's exprstat().
    std::expected<StatId, SyntaxError> parse_expression_statement();

    // 'local' attnamelist [ '=' explist ] (G3.1-G3.3). Lua's localstat().
    std::expected<StatId, SyntaxError> parse_local();

    // attrib ::= [ '<' Name '>' ] (G3.3, G3.15)
    std::expected<LocalName::Attribute, SyntaxError> parse_attribute();

    // ---- Functions (G5) ----

    // funcbody ::= '(' [ parlist ] ')' block 'end', after 'function' has
    // been read. `line` is the line of 'function'. A method gets `self` as
    // its first parameter (G3.11).
    std::expected<FunctionId, SyntaxError> parse_function_body(int line, bool is_method);

    // 'function' funcname funcbody (G3.5, G3.11)
    std::expected<StatId, SyntaxError> parse_function_statement();

    // 'local' 'function' Name funcbody, after 'local' (G3.12)
    std::expected<StatId, SyntaxError> parse_local_function(int line);

    // ---- Control flow (G3.1) ----

    // 'do' block 'end'
    std::expected<StatId, SyntaxError> parse_do();

    // 'while' exp 'do' block 'end'
    std::expected<StatId, SyntaxError> parse_while();

    // 'repeat' block 'until' exp
    std::expected<StatId, SyntaxError> parse_repeat();

    // 'if' exp 'then' block { 'elseif' exp 'then' block } [ 'else' block ] 'end'
    std::expected<StatId, SyntaxError> parse_if();

    // 'for' Name '=' ... (numeric) or 'for' namelist 'in' ... (generic)
    std::expected<StatId, SyntaxError> parse_for();

    // The rest of a numeric for, after `for Name`: '=' exp ',' exp [ ',' exp ]
    std::expected<StatId, SyntaxError> parse_numeric_for(TextRange variable, int line);

    // The rest of a generic for, after `for Name`: { ',' Name } 'in' explist
    std::expected<StatId, SyntaxError> parse_generic_for(TextRange first_name, int line);

    // 'do' block 'end', the body shared by both fors. `line` is the line of
    // 'for', for the "to close 'for'" message.
    std::expected<Block, SyntaxError> parse_for_body(int line);

    // explist ::= exp { ',' exp } (G4.6)
    std::expected<IdRange<ExprId>, SyntaxError> parse_expression_list();

    // exp (G4.1): a whole expression, operators included.
    std::expected<ExprId, SyntaxError> parse_expression();

    // An expression whose binary operators all bind tighter than `limit`
    // (precedence climbing, G7.8; Lua's subexpr()). Operators at or below the
    // limit are left for the caller.
    std::expected<ExprId, SyntaxError> parse_subexpression(int limit);

    // An operand without operators: nil, true, false, numerals, strings,
    // '...', or a suffixed expression.
    std::expected<ExprId, SyntaxError> parse_simple_expression();

    // A primary expression followed by any number of suffixes, applied left
    // to right: .name, [exp], :name args, args (G4.8). Lua's suffixedexp().
    std::expected<ExprId, SyntaxError> parse_suffixed_expression();

    // A Name or a parenthesized expression; nothing else can start a
    // prefixexp (G4.2, G4.7). Lua's primaryexp().
    std::expected<ExprId, SyntaxError> parse_primary_expression();

    // args ::= '(' [ explist ] ')' | tableconstructor | LiteralString (G4.5)
    std::expected<IdRange<ExprId>, SyntaxError> parse_call_arguments();

    // tableconstructor ::= '{' [ fieldlist ] '}' (G6). Lua's constructor().
    std::expected<ExprId, SyntaxError> parse_table();

    // One field: [exp] = exp, name = exp, or exp (G6.3).
    std::expected<TableField, SyntaxError> parse_table_field();

    // The current token must be a Name: moves past it and returns its text,
    // or the error "<name> expected".
    std::expected<TextRange, SyntaxError> expect_name();

    // Like expect(), for a closing token that pairs with an opening one at
    // `line`. When they are on different lines the error names the opener:
    // "')' expected (to close '(' at line 3)" (G9.3). Lua's check_match().
    std::expected<void, SyntaxError> expect_match(TokenKind closing, TokenKind opening, int line);

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

    // A semantic error: like error(), but without the " near ..." part
    // (G9.5). Lua's luaK_semerror().
    [[nodiscard]] std::unexpected<SyntaxError>
    semantic_error(std::string_view message,
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

    // For each function being parsed, innermost last: whether it is vararg,
    // so '...' can be checked (G4.12). The main chunk is vararg (G2.4).
    std::vector<bool> vararg_functions_{true};
};

} // namespace lua2
