#include "parser/ast.hpp"

namespace lua2 {

IdRange<StatId> Ast::add_statement_list(std::span<const StatId> ids) {
    const auto first = static_cast<std::uint32_t>(statement_lists_.size());
    statement_lists_.insert(statement_lists_.end(), ids.begin(), ids.end());
    return IdRange<StatId>{.first = first, .count = static_cast<std::uint32_t>(ids.size())};
}

std::span<const StatId> Ast::statements(const Block& block) const {
    return std::span{statement_lists_}.subspan(block.statements.first, block.statements.count);
}

} // namespace lua2
