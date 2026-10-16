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

    // { stat }: statements until the block ends or a 'return' comes. They
    // need no separator between them (G2.6).
    while (!block_follow() && !check(TokenKind::Return)) {
        auto statement = parse_statement();
        if (!statement) {
            return std::unexpected(std::move(statement.error()));
        }
        if (*statement) { // an empty statement ';' adds nothing
            statements.push_back(**statement);
        }
    }

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

std::expected<std::optional<StatId>, SyntaxError> Parser::parse_statement() {
    switch (current_.kind) {
    case TokenKind::Semicolon: { // an empty statement (G2.6)
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        return std::nullopt;
    }
    case TokenKind::Local: return parse_local();
    case TokenKind::Do: return parse_do();
    case TokenKind::While: return parse_while();
    case TokenKind::Repeat: return parse_repeat();
    case TokenKind::If: return parse_if();
    case TokenKind::For: return parse_for();
    case TokenKind::Function: return parse_function_statement();
    case TokenKind::Goto:
    case TokenKind::DoubleColon:
    case TokenKind::Break: return parse_jump();
    default: return parse_expression_statement();
    }
}

namespace {

// Whether an expression can be assigned to: a name or an indexing (G3.9).
bool is_variable(const Expr& expression) {
    return std::holds_alternative<NameExpr>(expression) ||
           std::holds_alternative<IndexExpr>(expression);
}

bool is_call(const Expr& expression) {
    return std::holds_alternative<CallExpr>(expression) ||
           std::holds_alternative<MethodCallExpr>(expression);
}

} // namespace

std::expected<StatId, SyntaxError> Parser::parse_expression_statement() {
    const int line = current_.line;
    auto first = parse_suffixed_expression();
    if (!first) {
        return std::unexpected(std::move(first.error()));
    }

    // An assignment: `var { ',' var } '=' explist`.
    if (check(TokenKind::Assign) || check(TokenKind::Comma)) {
        std::vector<ExprId> targets{*first};
        for (;;) {
            // Like Lua, each target is checked once the token after it is
            // read, so the error points there: `f() = 1` is "syntax error
            // near '='" (G3.9).
            if (!is_variable(ast_.expression(targets.back()))) {
                return error("syntax error");
            }
            auto comma = accept(TokenKind::Comma);
            if (!comma) {
                return std::unexpected(std::move(comma.error()));
            }
            if (!*comma) {
                break;
            }
            auto target = parse_suffixed_expression();
            if (!target) {
                return std::unexpected(std::move(target.error()));
            }
            targets.push_back(*target);
        }
        if (auto assign = expect(TokenKind::Assign); !assign) {
            return std::unexpected(std::move(assign.error()));
        }
        auto values = parse_expression_list();
        if (!values) {
            return std::unexpected(std::move(values.error()));
        }
        return ast_.add_statement(
            AssignStat{.targets = ast_.add_expression_list(targets), .values = *values}, line);
    }

    // Otherwise it must be a call; `x` or `a.b` alone does nothing, so Lua
    // refuses it (G3.8).
    if (!is_call(ast_.expression(*first))) {
        return error("syntax error");
    }
    return ast_.add_statement(CallStat{.call = *first}, line);
}

std::expected<StatId, SyntaxError> Parser::parse_local() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) { // 'local'
        return std::unexpected(std::move(moved.error()));
    }
    if (check(TokenKind::Function)) {
        return parse_local_function(line);
    }

    // attnamelist ::= Name attrib { ',' Name attrib } (G3.2)
    std::vector<LocalName> names;
    bool has_close = false;
    for (;;) {
        auto name = expect_name();
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        auto attribute = parse_attribute();
        if (!attribute) {
            return std::unexpected(std::move(attribute.error()));
        }
        // At most one <close> per statement (G3.15).
        if (*attribute == LocalName::Attribute::Close) {
            if (has_close) {
                return semantic_error("multiple to-be-closed variables in local list");
            }
            has_close = true;
        }
        names.push_back(LocalName{.name = *name, .attribute = *attribute});

        auto comma = accept(TokenKind::Comma);
        if (!comma) {
            return std::unexpected(std::move(comma.error()));
        }
        if (!*comma) {
            break;
        }
    }

    // [ '=' explist ]
    IdRange<ExprId> values;
    auto assign = accept(TokenKind::Assign);
    if (!assign) {
        return std::unexpected(std::move(assign.error()));
    }
    if (*assign) {
        auto list = parse_expression_list();
        if (!list) {
            return std::unexpected(std::move(list.error()));
        }
        values = *list;
    }
    return ast_.add_statement(LocalStat{.names = ast_.add_local_name_list(names), .values = values},
                              line);
}

std::expected<LocalName::Attribute, SyntaxError> Parser::parse_attribute() {
    auto open = accept(TokenKind::Less);
    if (!open) {
        return std::unexpected(std::move(open.error()));
    }
    if (!*open) {
        return LocalName::Attribute::None;
    }
    if (!check(TokenKind::Name)) {
        return error_expected(TokenKind::Name);
    }
    const std::string attribute = std::get<std::string>(current_.value);
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    if (auto close = expect(TokenKind::Greater); !close) {
        return std::unexpected(std::move(close.error()));
    }
    if (attribute == "const") {
        return LocalName::Attribute::Const;
    }
    if (attribute == "close") {
        return LocalName::Attribute::Close;
    }
    return semantic_error(std::format("unknown attribute '{}'", attribute)); // G3.15
}

// ---- Functions ----

std::expected<FunctionId, SyntaxError> Parser::parse_function_body(int line, bool is_method) {
    Function function;
    function.line = line;

    // parlist ::= namelist [ ',' '...' ] | '...' (G5.3). A method's `self`
    // comes first (G3.11).
    std::vector<LocalName> parameters;
    if (is_method) {
        parameters.push_back(LocalName{.name = ast_.add_text("self")});
    }
    if (auto open = expect(TokenKind::LParen); !open) {
        return std::unexpected(std::move(open.error()));
    }
    if (!check(TokenKind::RParen)) {
        for (;;) {
            if (check(TokenKind::Name)) {
                auto name = expect_name();
                if (!name) {
                    return std::unexpected(std::move(name.error()));
                }
                parameters.push_back(LocalName{.name = *name});
            } else if (check(TokenKind::Ellipsis)) {
                if (auto moved = advance(); !moved) {
                    return std::unexpected(std::move(moved.error()));
                }
                function.is_vararg = true;
                break; // '...' must be last
            } else {
                return error("<name> or '...' expected"); // G5.4
            }
            auto comma = accept(TokenKind::Comma);
            if (!comma) {
                return std::unexpected(std::move(comma.error()));
            }
            if (!*comma) {
                break;
            }
        }
    }
    if (auto close = expect(TokenKind::RParen); !close) {
        return std::unexpected(std::move(close.error()));
    }
    function.parameters = ast_.add_local_name_list(parameters);

    // The body sees '...' only if this function is vararg (G4.12).
    vararg_functions_.push_back(function.is_vararg);
    auto body = parse_block();
    vararg_functions_.pop_back();
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    function.body = *body;
    function.end_line = current_.line;
    if (auto closed = expect_match(TokenKind::End, TokenKind::Function, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return ast_.add_function(function);
}

std::expected<StatId, SyntaxError> Parser::parse_function_statement() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) { // 'function'
        return std::unexpected(std::move(moved.error()));
    }

    // funcname ::= Name { '.' Name } [ ':' Name ] (G3.5). It is built as the
    // variable being assigned: f, then f.a, then f.a.b, like a.b.c in an
    // expression.
    const int name_line = current_.line;
    auto first = expect_name();
    if (!first) {
        return std::unexpected(std::move(first.error()));
    }
    ExprId target = ast_.add_expression(NameExpr{.name = *first}, name_line);
    bool is_method = false;
    while (check(TokenKind::Dot) || check(TokenKind::Colon)) {
        is_method = check(TokenKind::Colon);
        const int field_line = current_.line;
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        const int key_line = current_.line;
        auto field = expect_name();
        if (!field) {
            return std::unexpected(std::move(field.error()));
        }
        const ExprId key = ast_.add_expression(StringExpr{.text = *field}, key_line);
        target = ast_.add_expression(IndexExpr{.object = target, .key = key}, field_line);
        if (is_method) {
            break; // the method name is the last part
        }
    }

    auto function = parse_function_body(line, is_method);
    if (!function) {
        return std::unexpected(std::move(function.error()));
    }
    return ast_.add_statement(
        FunctionStat{.target = target, .function = *function, .is_method = is_method}, line);
}

std::expected<StatId, SyntaxError> Parser::parse_local_function(int line) {
    if (auto moved = advance(); !moved) { // 'function'
        return std::unexpected(std::move(moved.error()));
    }
    auto name = expect_name();
    if (!name) {
        return std::unexpected(std::move(name.error()));
    }
    auto function = parse_function_body(line, false);
    if (!function) {
        return std::unexpected(std::move(function.error()));
    }
    return ast_.add_statement(LocalFunctionStat{.name = *name, .function = *function}, line);
}

// ---- goto, labels and break ----

std::expected<StatId, SyntaxError> Parser::parse_jump() {
    const int line = current_.line;
    const TokenKind keyword = current_.kind;
    if (auto moved = advance(); !moved) { // 'goto', '::' or 'break'
        return std::unexpected(std::move(moved.error()));
    }
    if (keyword == TokenKind::Break) {
        return ast_.add_statement(BreakStat{}, line);
    }

    auto name = expect_name();
    if (!name) {
        return std::unexpected(std::move(name.error()));
    }
    if (keyword == TokenKind::Goto) {
        return ast_.add_statement(GotoStat{.label = *name}, line);
    }
    // ::Name:: (G3.4)
    if (auto closed = expect(TokenKind::DoubleColon); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return ast_.add_statement(LabelStat{.name = *name}, line);
}

// ---- Control flow ----
//
// Each statement records the line of its keyword, and closes with
// expect_match(), so a missing 'end' on a later line names its opener:
// "'end' expected (to close 'while' at line 3) near <eof>".

std::expected<StatId, SyntaxError> Parser::parse_do() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    auto body = parse_block();
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    if (auto closed = expect_match(TokenKind::End, TokenKind::Do, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return ast_.add_statement(DoStat{.body = *body}, line);
}

std::expected<StatId, SyntaxError> Parser::parse_while() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    auto condition = parse_expression();
    if (!condition) {
        return std::unexpected(std::move(condition.error()));
    }
    if (auto keyword = expect(TokenKind::Do); !keyword) {
        return std::unexpected(std::move(keyword.error()));
    }
    auto body = parse_block();
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    if (auto closed = expect_match(TokenKind::End, TokenKind::While, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return ast_.add_statement(WhileStat{.condition = *condition, .body = *body}, line);
}

std::expected<StatId, SyntaxError> Parser::parse_repeat() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    auto body = parse_block(); // block_follow() stops at 'until'
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    if (auto closed = expect_match(TokenKind::Until, TokenKind::Repeat, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    auto condition = parse_expression();
    if (!condition) {
        return std::unexpected(std::move(condition.error()));
    }
    return ast_.add_statement(RepeatStat{.body = *body, .condition = *condition}, line);
}

std::expected<StatId, SyntaxError> Parser::parse_if() {
    const int line = current_.line;
    std::vector<IfClause> clauses;

    // The 'if' clause, then each 'elseif' clause: keyword, condition,
    // 'then', block.
    do {
        if (auto moved = advance(); !moved) { // 'if' or 'elseif'
            return std::unexpected(std::move(moved.error()));
        }
        auto condition = parse_expression();
        if (!condition) {
            return std::unexpected(std::move(condition.error()));
        }
        if (auto then = expect(TokenKind::Then); !then) {
            return std::unexpected(std::move(then.error()));
        }
        auto body = parse_block();
        if (!body) {
            return std::unexpected(std::move(body.error()));
        }
        clauses.push_back(IfClause{.condition = *condition, .body = *body});
    } while (check(TokenKind::ElseIf));

    IfStat statement; // no else until one is read
    statement.clauses = ast_.add_if_clause_list(clauses);
    auto has_else = accept(TokenKind::Else);
    if (!has_else) {
        return std::unexpected(std::move(has_else.error()));
    }
    if (*has_else) {
        auto body = parse_block();
        if (!body) {
            return std::unexpected(std::move(body.error()));
        }
        statement.else_body = *body;
        statement.has_else = true;
    }
    if (auto closed = expect_match(TokenKind::End, TokenKind::If, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return ast_.add_statement(statement, line);
}

std::expected<StatId, SyntaxError> Parser::parse_for() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    auto first_name = expect_name();
    if (!first_name) {
        return std::unexpected(std::move(first_name.error()));
    }
    // The token after the first name decides which for it is (G3.10).
    switch (current_.kind) {
    case TokenKind::Assign: return parse_numeric_for(*first_name, line);
    case TokenKind::Comma:
    case TokenKind::In: return parse_generic_for(*first_name, line);
    default: return error("'=' or 'in' expected");
    }
}

std::expected<StatId, SyntaxError> Parser::parse_numeric_for(TextRange variable, int line) {
    if (auto moved = advance(); !moved) { // '='
        return std::unexpected(std::move(moved.error()));
    }
    NumericForStat statement; // filled in as the parts are read
    statement.variable = variable;

    auto start = parse_expression();
    if (!start) {
        return std::unexpected(std::move(start.error()));
    }
    statement.start = *start;
    if (auto comma = expect(TokenKind::Comma); !comma) {
        return std::unexpected(std::move(comma.error()));
    }
    auto limit = parse_expression();
    if (!limit) {
        return std::unexpected(std::move(limit.error()));
    }
    statement.limit = *limit;

    // The step is optional (it defaults to 1 at run time).
    auto has_step = accept(TokenKind::Comma);
    if (!has_step) {
        return std::unexpected(std::move(has_step.error()));
    }
    if (*has_step) {
        auto step = parse_expression();
        if (!step) {
            return std::unexpected(std::move(step.error()));
        }
        statement.step = *step;
        statement.has_step = true;
    }

    auto body = parse_for_body(line);
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    statement.body = *body;
    return ast_.add_statement(statement, line);
}

std::expected<StatId, SyntaxError> Parser::parse_generic_for(TextRange first_name, int line) {
    std::vector<LocalName> names{LocalName{.name = first_name}};
    for (;;) {
        auto comma = accept(TokenKind::Comma);
        if (!comma) {
            return std::unexpected(std::move(comma.error()));
        }
        if (!*comma) {
            break;
        }
        auto name = expect_name();
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        names.push_back(LocalName{.name = *name});
    }
    if (auto keyword = expect(TokenKind::In); !keyword) {
        return std::unexpected(std::move(keyword.error()));
    }
    auto values = parse_expression_list();
    if (!values) {
        return std::unexpected(std::move(values.error()));
    }
    auto body = parse_for_body(line);
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    return ast_.add_statement(
        GenericForStat{.names = ast_.add_local_name_list(names), .values = *values, .body = *body},
        line);
}

std::expected<Block, SyntaxError> Parser::parse_for_body(int line) {
    if (auto keyword = expect(TokenKind::Do); !keyword) {
        return std::unexpected(std::move(keyword.error()));
    }
    auto body = parse_block();
    if (!body) {
        return body;
    }
    if (auto closed = expect_match(TokenKind::End, TokenKind::For, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return body;
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
    case TokenKind::Ellipsis:
        // Only a vararg function has extra arguments to give (G4.12).
        if (!vararg_functions_.back()) {
            return error("cannot use '...' outside a vararg function");
        }
        expression = VarargExpr{};
        break;
    case TokenKind::Integer:
        expression = IntegerExpr{.value = std::get<std::int64_t>(current_.value)};
        break;
    case TokenKind::Float: expression = FloatExpr{.value = std::get<double>(current_.value)}; break;
    case TokenKind::String:
        expression = StringExpr{.text = ast_.add_text(std::get<std::string>(current_.value))};
        break;
    case TokenKind::LBrace: return parse_table();
    case TokenKind::Function: { // function funcbody (G5.1)
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        auto function = parse_function_body(line, false);
        if (!function) {
            return std::unexpected(std::move(function.error()));
        }
        return ast_.add_expression(FunctionExpr{.function = *function}, line);
    }
    default:
        // Names and '(' start a suffixed expression; anything else is
        // "unexpected symbol" there (G4.7).
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
        case TokenKind::LBrace:
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
    case TokenKind::LBrace:   // f{...}: exactly one argument, a table (G4.10)
    case TokenKind::String: { // f"str": exactly one argument, a string (G4.10)
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

std::expected<ExprId, SyntaxError> Parser::parse_table() {
    const int line = current_.line;
    if (auto moved = advance(); !moved) { // '{'
        return std::unexpected(std::move(moved.error()));
    }

    // fieldlist ::= field { fieldsep field } [ fieldsep ] (G6.2). After each
    // field, a ',' or ';' allows another; without one the table must end. A
    // separator right before '}' is allowed (G6.7).
    std::vector<TableField> fields;
    while (!check(TokenKind::RBrace)) {
        auto field = parse_table_field();
        if (!field) {
            return std::unexpected(std::move(field.error()));
        }
        fields.push_back(*field);

        auto comma = accept(TokenKind::Comma);
        if (!comma) {
            return std::unexpected(std::move(comma.error()));
        }
        if (*comma) {
            continue;
        }
        auto semicolon = accept(TokenKind::Semicolon);
        if (!semicolon) {
            return std::unexpected(std::move(semicolon.error()));
        }
        if (!*semicolon) {
            break;
        }
    }
    if (auto closed = expect_match(TokenKind::RBrace, TokenKind::LBrace, line); !closed) {
        return std::unexpected(std::move(closed.error()));
    }
    return ast_.add_expression(TableExpr{.fields = ast_.add_field_list(fields)}, line);
}

std::expected<TableField, SyntaxError> Parser::parse_table_field() {
    // name = exp. To tell `{x = 1}` from `{x}` or `{x == 1}`, look one token
    // past the name: only an '=' makes it a named field (G6.5). This is the
    // one place in Lua's grammar that needs two tokens of lookahead.
    if (check(TokenKind::Name)) {
        auto next = peek();
        if (!next) {
            return std::unexpected(std::move(next.error()));
        }
        if ((*next)->kind == TokenKind::Assign) {
            const int line = current_.line;
            auto name = expect_name();
            if (!name) {
                return std::unexpected(std::move(name.error()));
            }
            const ExprId key = ast_.add_expression(StringExpr{.text = *name}, line);
            if (auto moved = advance(); !moved) { // '='
                return std::unexpected(std::move(moved.error()));
            }
            auto value = parse_expression();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            return TableField{.kind = TableField::Kind::Named, .key = key, .value = *value};
        }
    }

    // [exp] = exp
    if (check(TokenKind::LBracket)) {
        if (auto moved = advance(); !moved) {
            return std::unexpected(std::move(moved.error()));
        }
        auto key = parse_expression();
        if (!key) {
            return std::unexpected(std::move(key.error()));
        }
        if (auto closed = expect(TokenKind::RBracket); !closed) {
            return std::unexpected(std::move(closed.error()));
        }
        if (auto assign = expect(TokenKind::Assign); !assign) {
            return std::unexpected(std::move(assign.error()));
        }
        auto value = parse_expression();
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return TableField{.kind = TableField::Kind::Keyed, .key = *key, .value = *value};
    }

    // exp: a positional field.
    auto value = parse_expression();
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    TableField field; // Positional by default, and the key stays unused
    field.value = *value;
    return field;
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

std::unexpected<SyntaxError> Parser::semantic_error(std::string_view message,
                                                    std::source_location origin) const {
    return std::unexpected(
        make_syntax_error(lexer_.chunk_name(), current_.line, message, "", origin));
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
