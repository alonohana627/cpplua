#include "parser/ast.hpp"

#include <utility>

namespace lua2 {

namespace {

// Ids are 32 bits. A chunk is limited to 4 GiB of source (see Token::offset),
// and every node takes at least one token, so the counts always fit.
template <typename Id, typename T>
Id push(std::vector<T>& nodes, T node) {
    const auto index = static_cast<std::uint32_t>(nodes.size());
    nodes.push_back(std::move(node));
    return Id{.index = index};
}

template <typename Id>
IdRange<Id> append(std::vector<Id>& lists, std::span<const Id> ids) {
    const auto first = static_cast<std::uint32_t>(lists.size());
    lists.insert(lists.end(), ids.begin(), ids.end());
    return IdRange<Id>{.first = first, .count = static_cast<std::uint32_t>(ids.size())};
}

} // namespace

ExprId Ast::add_expression(Expr expr, int line) {
    expression_lines_.push_back(line);
    return push<ExprId>(expressions_, expr);
}

StatId Ast::add_statement(Stat stat, int line) {
    statement_lines_.push_back(line);
    return push<StatId>(statements_, stat);
}

IdRange<StatId> Ast::add_statement_list(std::span<const StatId> ids) {
    return append(statement_lists_, ids);
}

IdRange<ExprId> Ast::add_expression_list(std::span<const ExprId> ids) {
    return append(expression_lists_, ids);
}

std::span<const StatId> Ast::statements(const Block& block) const {
    return std::span{statement_lists_}.subspan(block.statements.first, block.statements.count);
}

std::span<const ExprId> Ast::expressions(IdRange<ExprId> range) const {
    return std::span{expression_lists_}.subspan(range.first, range.count);
}

TextRange Ast::add_text(std::string_view bytes) {
    const auto first = static_cast<std::uint32_t>(text_.size());
    text_ += bytes;
    return TextRange{.first = first, .length = static_cast<std::uint32_t>(bytes.size())};
}

std::string_view Ast::text(TextRange range) const {
    return std::string_view{text_}.substr(range.first, range.length);
}

std::string_view operator_text(UnaryOp op) {
    switch (op) {
    case UnaryOp::Negate: return "-";
    case UnaryOp::Not: return "not";
    case UnaryOp::Length: return "#";
    case UnaryOp::BitwiseNot: return "~";
    }
    return "?"; // only reachable with an invalid enum value
}

std::string_view operator_text(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Subtract: return "-";
    case BinaryOp::Multiply: return "*";
    case BinaryOp::Divide: return "/";
    case BinaryOp::FloorDivide: return "//";
    case BinaryOp::Modulo: return "%";
    case BinaryOp::Power: return "^";
    case BinaryOp::Concat: return "..";
    case BinaryOp::Equal: return "==";
    case BinaryOp::NotEqual: return "~=";
    case BinaryOp::Less: return "<";
    case BinaryOp::LessEqual: return "<=";
    case BinaryOp::Greater: return ">";
    case BinaryOp::GreaterEqual: return ">=";
    case BinaryOp::And: return "and";
    case BinaryOp::Or: return "or";
    case BinaryOp::BitwiseAnd: return "&";
    case BinaryOp::BitwiseOr: return "|";
    case BinaryOp::BitwiseXor: return "~";
    case BinaryOp::ShiftLeft: return "<<";
    case BinaryOp::ShiftRight: return ">>";
    }
    return "?"; // only reachable with an invalid enum value
}

} // namespace lua2
