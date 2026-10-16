#include "parser/parser.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lua2 {

Parser::Parser(std::string_view source, std::string_view chunk_name)
    : source_(source), lexer_(source, chunk_name) {}

std::expected<Ast, SyntaxError> Parser::parse_chunk() {
    // Load the first token.
    if (auto loaded = advance(); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }

    // chunk ::= block <eof> (G2.1)
    auto block = parse_block();
    if (!block) {
        return std::unexpected(std::move(block.error()));
    }
    // Anything left after the block is an error: "<eof> expected near 'x'"
    // (G2.5, G9.3).
    if (!check(TokenKind::EndOfStream)) {
        return error_expected(TokenKind::EndOfStream);
    }
    ast_.set_root(*block);
    return std::move(ast_);
}

std::expected<Block, SyntaxError> Parser::parse_block() {
    // The statements are collected here first, then stored in the Ast in one
    // go, so they sit next to each other (see Ast::add_statement_list).
    std::vector<StatId> statements;

    // { stat }: the other statements arrive in parts 5 to 8, as a loop here
    // that runs until block_follow() or 'return'. Until then, a block holds at
    // most a return, and anything else is left for the caller to report.

    // [ retstat ]: 'return' can only be the last statement of its block
    // (G2.5). The caller then expects the block's end, so `return 1 x` fails
    // there with "<eof> expected near 'x'" (or "'end' expected").
    if (check(TokenKind::Return)) {
        auto statement = parse_return();
        if (!statement) {
            return std::unexpected(std::move(statement.error()));
        }
        statements.push_back(*statement);
    }
    return Block{.statements = ast_.add_statement_list(statements)};
}

std::expected<StatId, SyntaxError> Parser::parse_return() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) { // 'return'
        return std::unexpected(std::move(moved.error()));
    }

    // The values are optional: `return` alone, or followed directly by the
    // end of the block or a ';', returns nothing.
    IdRange<ExprId> values;
    if (!block_follow() && !check(TokenKind::Semicolon)) {
        auto list = parse_expression_list();
        if (!list) {
            return std::unexpected(std::move(list.error()));
        }
        values = *list;
    }
    // One optional ';' (G2.3).
    if (auto semicolon = accept(TokenKind::Semicolon); !semicolon) {
        return std::unexpected(std::move(semicolon.error()));
    }
    return ast_.add_statement(ReturnStat{.values = values}, line);
}

std::expected<IdRange<ExprId>, SyntaxError> Parser::parse_expression_list() {
    std::vector<ExprId> expressions;
    for (;;) {
        auto expression = parse_expression();
        if (!expression) {
            return std::unexpected(std::move(expression.error()));
        }
        expressions.push_back(*expression);

        auto comma = accept(TokenKind::Comma);
        if (!comma) {
            return std::unexpected(std::move(comma.error()));
        }
        if (!*comma) {
            return ast_.add_expression_list(expressions);
        }
    }
}

namespace {

// ---- Operator tables (G7.3, G7.8) ----

std::optional<UnaryOp> unary_operator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Minus: return UnaryOp::Negate;
    case TokenKind::Not: return UnaryOp::Not;
    case TokenKind::Hash: return UnaryOp::Length;
    case TokenKind::Tilde: return UnaryOp::BitwiseNot;
    default: return std::nullopt;
    }
}

std::optional<BinaryOp> binary_operator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Plus: return BinaryOp::Add;
    case TokenKind::Minus: return BinaryOp::Subtract;
    case TokenKind::Star: return BinaryOp::Multiply;
    case TokenKind::Slash: return BinaryOp::Divide;
    case TokenKind::DoubleSlash: return BinaryOp::FloorDivide;
    case TokenKind::Percent: return BinaryOp::Modulo;
    case TokenKind::Caret: return BinaryOp::Power;
    case TokenKind::Concat: return BinaryOp::Concat;
    case TokenKind::Equal: return BinaryOp::Equal;
    case TokenKind::NotEqual: return BinaryOp::NotEqual;
    case TokenKind::Less: return BinaryOp::Less;
    case TokenKind::LessEqual: return BinaryOp::LessEqual;
    case TokenKind::Greater: return BinaryOp::Greater;
    case TokenKind::GreaterEqual: return BinaryOp::GreaterEqual;
    case TokenKind::And: return BinaryOp::And;
    case TokenKind::Or: return BinaryOp::Or;
    case TokenKind::Ampersand: return BinaryOp::BitwiseAnd;
    case TokenKind::Pipe: return BinaryOp::BitwiseOr;
    case TokenKind::Tilde: return BinaryOp::BitwiseXor;
    case TokenKind::ShiftLeft: return BinaryOp::ShiftLeft;
    case TokenKind::ShiftRight: return BinaryOp::ShiftRight;
    default: return std::nullopt;
    }
}

// Lua's priority table (lparser.c). An operator binds its *left* operand
// with `left`, and parses its right operand with `right` as the new limit.
// A right priority lower than the left one makes the operator
// right-associative: .. and ^.
struct Priority {
    int left;
    int right;
};

constexpr Priority priority(BinaryOp op) {
    switch (op) {
    case BinaryOp::Or: return {.left = 1, .right = 1};
    case BinaryOp::And: return {.left = 2, .right = 2};
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual: return {.left = 3, .right = 3};
    case BinaryOp::BitwiseOr: return {.left = 4, .right = 4};
    case BinaryOp::BitwiseXor: return {.left = 5, .right = 5};
    case BinaryOp::BitwiseAnd: return {.left = 6, .right = 6};
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight: return {.left = 7, .right = 7};
    case BinaryOp::Concat: return {.left = 9, .right = 8}; // right-associative
    case BinaryOp::Add:
    case BinaryOp::Subtract: return {.left = 10, .right = 10};
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
    case BinaryOp::FloorDivide:
    case BinaryOp::Modulo: return {.left = 11, .right = 11};
    case BinaryOp::Power: return {.left = 14, .right = 13}; // right-associative
    }
    return {.left = 0, .right = 0}; // only reachable with an invalid enum value
}

// The operand of a unary operator is parsed with this limit, between '%' and
// '^': so -x^2 is -(x^2), but -x*y is (-x)*y (G7.4).
constexpr int unary_priority = 12;

} // namespace

std::expected<ExprId, SyntaxError> Parser::parse_expression() {
    return parse_subexpression(0);
}

std::expected<ExprId, SyntaxError> Parser::parse_subexpression(int limit) {
    // 1. One operand: a unary operator and its own operand, or a simple
    //    expression.
    ExprId left;
    if (const auto unary = unary_operator(current_.kind)) {
        const int line = current_.line;
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        auto operand = parse_subexpression(unary_priority);
        if (!operand) {
            return std::unexpected(std::move(operand.error()));
        }
        left = ast_.add_expression(UnaryExpr{.op = *unary, .operand = *operand}, line);
    } else {
        auto simple = parse_simple_expression();
        if (!simple) {
            return std::unexpected(std::move(simple.error()));
        }
        left = *simple;
    }

    // 2. While the next token is a binary operator that binds tighter than
    //    the limit, take it, and parse its right side with its own right
    //    priority as the new limit. The result becomes the new left side,
    //    which makes equal operators group to the left: 1-2-3 is (1-2)-3.
    for (;;) {
        const auto binary = binary_operator(current_.kind);
        if (!binary || priority(*binary).left <= limit) {
            return left;
        }
        const int line = current_.line; // a binary node records its operator's line
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        auto right = parse_subexpression(priority(*binary).right);
        if (!right) {
            return std::unexpected(std::move(right.error()));
        }
        left = ast_.add_expression(BinaryExpr{.op = *binary, .left = left, .right = *right}, line);
    }
}

std::expected<ExprId, SyntaxError> Parser::parse_simple_expression() {
    const int line = current_.line;
    Expr expression;
    switch (current_.kind) {
    case TokenKind::Nil: expression = NilExpr{}; break;
    case TokenKind::True: expression = TrueExpr{}; break;
    case TokenKind::False: expression = FalseExpr{}; break;
    case TokenKind::Ellipsis: expression = VarargExpr{}; break;
    case TokenKind::Integer:
        expression = IntegerExpr{.value = std::get<std::int64_t>(current_.value)};
        break;
    case TokenKind::Float: expression = FloatExpr{.value = std::get<double>(current_.value)}; break;
    case TokenKind::String:
        expression = StringExpr{.text = ast_.add_text(std::get<std::string>(current_.value))};
        break;
    default:
        // Names and '(' start a suffixed expression; anything else is
        // "unexpected symbol" there (G4.7). Tables and functions arrive in
        // parts 4 and 7.
        return parse_suffixed_expression();
    }
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    return ast_.add_expression(expression, line);
}

std::expected<ExprId, SyntaxError> Parser::parse_suffixed_expression() {
    const int line = current_.line; // a call records the line its expression starts on
    auto primary = parse_primary_expression();
    if (!primary) {
        return primary;
    }
    ExprId expression = *primary;

    for (;;) {
        switch (current_.kind) {
        case TokenKind::Dot: { // .name, the same as ["name"]
            const int dot_line = current_.line;
            if (auto moved = advance(); !moved) {
                return std::unexpected(std::move(moved.error()));
            }
            const int name_line = current_.line;
            auto name = expect_name();
            if (!name) {
                return std::unexpected(std::move(name.error()));
            }
            const ExprId key = ast_.add_expression(StringExpr{.text = *name}, name_line);
            expression = ast_.add_expression(IndexExpr{.object = expression, .key = key}, dot_line);
            break;
        }
        case TokenKind::LBracket: { // [exp]
            const int bracket_line = current_.line;
            if (auto moved = advance(); !moved) {
                return std::unexpected(std::move(moved.error()));
            }
            auto key = parse_expression();
            if (!key) {
                return key;
            }
            if (auto closed = expect(TokenKind::RBracket); !closed) {
                return std::unexpected(std::move(closed.error()));
            }
            expression =
                ast_.add_expression(IndexExpr{.object = expression, .key = *key}, bracket_line);
            break;
        }
        case TokenKind::Colon: { // :name args
            if (auto moved = advance(); !moved) {
                return std::unexpected(std::move(moved.error()));
            }
            auto method = expect_name();
            if (!method) {
                return std::unexpected(std::move(method.error()));
            }
            auto arguments = parse_call_arguments();
            if (!arguments) {
                return std::unexpected(std::move(arguments.error()));
            }
            expression = ast_.add_expression(
                MethodCallExpr{.object = expression, .method = *method, .arguments = *arguments},
                line);
            break;
        }
        case TokenKind::LParen:
        case TokenKind::String: { // args
            auto arguments = parse_call_arguments();
            if (!arguments) {
                return std::unexpected(std::move(arguments.error()));
            }
            expression = ast_.add_expression(
                CallExpr{.function = expression, .arguments = *arguments}, line);
            break;
        }
        default: return expression;
        }
    }
}

std::expected<ExprId, SyntaxError> Parser::parse_primary_expression() {
    const int line = current_.line;
    switch (current_.kind) {
    case TokenKind::Name: {
        auto name = expect_name();
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        return ast_.add_expression(NameExpr{.name = *name}, line);
    }
    case TokenKind::LParen: {
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        auto inner = parse_expression();
        if (!inner) {
            return inner;
        }
        if (auto closed = expect_match(TokenKind::RParen, TokenKind::LParen, line); !closed) {
            return std::unexpected(std::move(closed.error()));
        }
        return ast_.add_expression(ParenExpr{.inner = *inner}, line);
    }
    default: return error("unexpected symbol"); // G4.7
    }
}

std::expected<IdRange<ExprId>, SyntaxError> Parser::parse_call_arguments() {
    switch (current_.kind) {
    case TokenKind::String: { // f"str": exactly one argument (G4.10)
        auto argument = parse_simple_expression();
        if (!argument) {
            return std::unexpected(std::move(argument.error()));
        }
        return ast_.add_expression_list(std::span{&*argument, 1});
    }
    case TokenKind::LParen: {
        const int open_line = current_.line;
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        IdRange<ExprId> arguments;
        if (!check(TokenKind::RParen)) {
            auto list = parse_expression_list();
            if (!list) {
                return list;
            }
            arguments = *list;
        }
        if (auto closed = expect_match(TokenKind::RParen, TokenKind::LParen, open_line); !closed) {
            return std::unexpected(std::move(closed.error()));
        }
        return arguments;
    }
    default:
        // After obj:name the arguments are required (G4.9).
        return error("function arguments expected");
    }
}

std::expected<TextRange, SyntaxError> Parser::expect_name() {
    if (!check(TokenKind::Name)) {
        return error_expected(TokenKind::Name);
    }
    const TextRange name = ast_.add_text(std::get<std::string>(current_.value));
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    return name;
}

std::expected<void, SyntaxError> Parser::expect_match(TokenKind closing, TokenKind opening,
                                                      int line) {
    if (check(closing)) {
        return advance();
    }
    // On the same line, the short form is clearer (Lua's check_match).
    if (line == current_.line) {
        return error_expected(closing);
    }
    return error(std::format("{} expected (to close {} at line {})", expected_text(closing),
                             expected_text(opening), line));
}

bool Parser::block_follow() const noexcept {
    switch (current_.kind) {
    case TokenKind::EndOfStream:
    case TokenKind::End:
    case TokenKind::Else:
    case TokenKind::ElseIf:
    case TokenKind::Until: return true;
    default: return false;
    }
}

// ---- Moving through tokens ----

std::expected<void, SyntaxError> Parser::advance() {
    if (lookahead_) {
        current_ = std::move(*lookahead_);
        lookahead_.reset();
        return {};
    }
    auto token = lexer_.next();
    if (!token) {
        return std::unexpected(std::move(token.error()));
    }
    current_ = std::move(*token);
    return {};
}

std::expected<const Token*, SyntaxError> Parser::peek() {
    if (!lookahead_) {
        auto token = lexer_.next();
        if (!token) {
            return std::unexpected(std::move(token.error()));
        }
        lookahead_ = std::move(*token);
    }
    return &*lookahead_;
}

std::expected<bool, SyntaxError> Parser::accept(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    return true;
}

std::expected<void, SyntaxError> Parser::expect(TokenKind kind) {
    if (!check(kind)) {
        return error_expected(kind);
    }
    return advance();
}

// ---- Errors ----

std::unexpected<SyntaxError> Parser::error(std::string_view message,
                                           std::source_location origin) const {
    return std::unexpected(
        make_syntax_error(lexer_.chunk_name(), current_.line, message, near_current(), origin));
}

std::unexpected<SyntaxError> Parser::error_expected(TokenKind kind,
                                                    std::source_location origin) const {
    return error(std::format("{} expected", expected_text(kind)), origin);
}

std::string Parser::near_current() const {
    // G9.2: <eof> at the end, and otherwise the token as written, in quotes.
    if (current_.kind == TokenKind::EndOfStream) {
        return std::string{near_eof};
    }
    const std::string_view text = source_.substr(current_.offset, current_.length);
    // A stray byte shows as '@', or as its value when unprintable: '<\200>'.
    if (current_.kind == TokenKind::Other) {
        return near_char(static_cast<unsigned char>(text.front()));
    }
    // A short string is the exception: like Lua, show its value after escapes,
    // between its own quotes ("\65" shows as '"A"'). A long string keeps its
    // text as written, brackets included.
    if (current_.kind == TokenKind::String && (text.front() == '"' || text.front() == '\'')) {
        const char quote = text.front();
        return near_text(
            std::format("{}{}{}", quote, std::get<std::string>(current_.value), quote));
    }
    return near_text(text);
}

} // namespace lua2
