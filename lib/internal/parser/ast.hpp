#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace lua2 {

// The abstract syntax tree, stored as an arena (see docs/ROADMAP.md,
// "Decisions"): every node lives in a std::vector owned by one Ast, and nodes
// refer to each other by small typed indices instead of pointers.
//
// Part 0 has only the skeleton: ids, ranges, Block, and the Ast that owns
// them. Each later part adds its node structs and their vectors.

// A typed index into one of the Ast's node vectors. The Tag makes ExprId and
// StatId different types, so passing one where the other is expected does not
// compile. 32 bits halve the size of a pointer.
template <typename Tag>
struct NodeId {
    std::uint32_t index = 0;

    bool operator==(const NodeId&) const = default;
};

struct ExprTag {};

struct StatTag {};

using ExprId = NodeId<ExprTag>; // an expression node
using StatId = NodeId<StatTag>; // a statement node

// A run of consecutive ids in one of the Ast's list vectors, such as the
// statements of a block. Storing (first, count) instead of a std::vector in
// every block keeps all lists in one allocation.
template <typename Id>
struct IdRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    bool operator==(const IdRange&) const = default;
};

// A list of statements (G2.2).
struct Block {
    IdRange<StatId> statements;
};

class Ast {
public:
    // The block of the whole chunk (G2.1).
    [[nodiscard]] const Block& root() const noexcept { return root_; }

    void set_root(Block block) noexcept { root_ = block; }

    // Copies `ids` to the end of the statement-list vector and returns where
    // they went. A block's statements are parsed into a temporary list first
    // (nested blocks add their own lists in between), then stored in one go,
    // so every block's statements end up next to each other.
    IdRange<StatId> add_statement_list(std::span<const StatId> ids);

    // The statements of a block, in order.
    [[nodiscard]] std::span<const StatId> statements(const Block& block) const;

private:
    Block root_;
    std::vector<StatId> statement_lists_;
};

} // namespace lua2
