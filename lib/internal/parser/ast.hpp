#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace lua2 {

// The abstract syntax tree, stored as an arena (see docs/ROADMAP.md,
// "Decisions"): every node lives in a std::vector owned by one Ast, and nodes
// refer to each other by small typed indices instead of pointers.
//
// Each part of the parser plan adds its node structs to the Expr and Stat
// variants below.

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

struct FieldTag {};

struct LocalNameTag {};

using ExprId = NodeId<ExprTag>;           // an expression node
using StatId = NodeId<StatTag>;           // a statement node
using FieldId = NodeId<FieldTag>;         // a field of a table constructor
using LocalNameId = NodeId<LocalNameTag>; // a name declared by `local`

// A run of consecutive ids in one of the Ast's list vectors, such as the
// statements of a block. Storing (first, count) instead of a std::vector in
// every node keeps all lists in one allocation.
template <typename Id>
struct IdRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    bool operator==(const IdRange&) const = default;
};

// A run of bytes in the Ast's text buffer, such as a string literal's value.
// All text lives in one std::string, so a node holds 8 bytes, not a string.
struct TextRange {
    std::uint32_t first = 0;
    std::uint32_t length = 0;

    bool operator==(const TextRange&) const = default;
};

// ---- Expressions (G4.1) ----

struct NilExpr {
    bool operator==(const NilExpr&) const = default;
};

struct TrueExpr {
    bool operator==(const TrueExpr&) const = default;
};

struct FalseExpr {
    bool operator==(const FalseExpr&) const = default;
};

struct IntegerExpr {
    std::int64_t value = 0;

    bool operator==(const IntegerExpr&) const = default;
};

struct FloatExpr {
    double value = 0.0;

    bool operator==(const FloatExpr&) const = default;
};

struct StringExpr {
    TextRange text; // the bytes after escapes, in the Ast's text buffer

    bool operator==(const StringExpr&) const = default;
};

// `...`, the extra arguments of a vararg function.
struct VarargExpr {
    bool operator==(const VarargExpr&) const = default;
};

// The unary operators (G7.2).
enum class UnaryOp : std::uint8_t {
    Negate,     // -x
    Not,        // not x
    Length,     // #x
    BitwiseNot, // ~x
};

// The binary operators (G7.1).
enum class BinaryOp : std::uint8_t {
    Add,          // +
    Subtract,     // -
    Multiply,     // *
    Divide,       // /  (always a float)
    FloorDivide,  // //
    Modulo,       // %
    Power,        // ^  (always a float, right-associative)
    Concat,       // .. (right-associative)
    Equal,        // ==
    NotEqual,     // ~=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=
    And,          // and (short-circuit)
    Or,           // or  (short-circuit)
    BitwiseAnd,   // &
    BitwiseOr,    // |
    BitwiseXor,   // ~
    ShiftLeft,    // <<
    ShiftRight,   // >>
};

// How an operator is written in Lua: "-", "not", "..".
std::string_view operator_text(UnaryOp op);
std::string_view operator_text(BinaryOp op);

struct UnaryExpr {
    UnaryOp op = UnaryOp::Negate;
    ExprId operand;

    bool operator==(const UnaryExpr&) const = default;
};

struct BinaryExpr {
    BinaryOp op = BinaryOp::Add;
    ExprId left;
    ExprId right;

    bool operator==(const BinaryExpr&) const = default;
};

// A variable name (G4.3). Whether it is a local, an upvalue or a global is
// decided in part 10.
struct NameExpr {
    TextRange name;

    bool operator==(const NameExpr&) const = default;
};

// `(exp)`. Kept as its own node, because the parentheses change the meaning:
// they cut a call or '...' down to one value, and the result is never a
// variable (G4.11).
struct ParenExpr {
    ExprId inner;

    bool operator==(const ParenExpr&) const = default;
};

// `object[key]`, and `object.name`, which is the same as `object["name"]`
// (G4.3). For `.name` the key is a StringExpr.
struct IndexExpr {
    ExprId object;
    ExprId key;

    bool operator==(const IndexExpr&) const = default;
};

// `function(arguments)` (G4.4). Also `f"str"`, `f[[str]]` and (part 4) `f{...}`,
// which pass exactly one argument (G4.10).
struct CallExpr {
    ExprId function;
    IdRange<ExprId> arguments;

    bool operator==(const CallExpr&) const = default;
};

// `object:method(arguments)` (G4.9): calls object.method with object as an
// extra first argument, evaluating object once.
struct MethodCallExpr {
    ExprId object;
    TextRange method;
    IdRange<ExprId> arguments;

    bool operator==(const MethodCallExpr&) const = default;
};

// One field of a table constructor (G6.3).
struct TableField {
    enum class Kind : std::uint8_t {
        Positional, // exp: gets the next key 1, 2, 3, ... (G6.6)
        Named,      // name = exp: the key is a StringExpr with the name (G6.5)
        Keyed,      // [exp] = exp
    };

    Kind kind = Kind::Positional;
    ExprId key; // unused for Positional
    ExprId value;

    bool operator==(const TableField&) const = default;
};

// `{ fields }` (G6.1). The fields are stored in the Ast's field-list vector.
struct TableExpr {
    IdRange<FieldId> fields;

    bool operator==(const TableExpr&) const = default;
};

using Expr = std::variant<NilExpr, TrueExpr, FalseExpr, IntegerExpr, FloatExpr, StringExpr,
                          VarargExpr, UnaryExpr, BinaryExpr, NameExpr, ParenExpr, IndexExpr,
                          CallExpr, MethodCallExpr, TableExpr>;

// ---- Statements (G3.1) ----

// `return explist` (G2.3). Always the last statement of its block.
struct ReturnStat {
    IdRange<ExprId> values;

    bool operator==(const ReturnStat&) const = default;
};

// A function call used as a statement (G3.1): the expression is a CallExpr
// or a MethodCallExpr, and its results are thrown away.
struct CallStat {
    ExprId call;

    bool operator==(const CallStat&) const = default;
};

// `varlist = explist` (G3.1, G3.6). Every target is a NameExpr or an
// IndexExpr (G3.9).
struct AssignStat {
    IdRange<ExprId> targets;
    IdRange<ExprId> values;

    bool operator==(const AssignStat&) const = default;
};

// A name declared by `local`, with its attribute (G3.2, G3.3, G3.15).
struct LocalName {
    enum class Attribute : std::uint8_t {
        None,
        Const, // <const>: cannot be assigned to
        Close, // <close>: closed (its __close metamethod runs) when it goes out of scope
    };

    TextRange name;
    Attribute attribute = Attribute::None;

    bool operator==(const LocalName&) const = default;
};

// `local attnamelist [ '=' explist ]` (G3.1). No '=' means no values.
struct LocalStat {
    IdRange<LocalNameId> names;
    IdRange<ExprId> values;

    bool operator==(const LocalStat&) const = default;
};

using Stat = std::variant<ReturnStat, CallStat, AssignStat, LocalStat>;

// Nodes hold only numbers and ids, never strings or pointers, so copying one
// is a plain memory copy and the vectors never run destructors. Keep it so.
static_assert(std::is_trivially_copyable_v<Expr>);
static_assert(std::is_trivially_copyable_v<Stat>);

// A list of statements (G2.2).
struct Block {
    IdRange<StatId> statements;
};

class Ast {
public:
    // The block of the whole chunk (G2.1).
    [[nodiscard]] const Block& root() const noexcept { return root_; }

    void set_root(Block block) noexcept { root_ = block; }

    // ---- Nodes ----
    // Every node records the line it starts on, kept in a parallel vector
    // (structure of arrays) so the node variants stay small.

    ExprId add_expression(Expr expr, int line);

    [[nodiscard]] const Expr& expression(ExprId id) const { return expressions_[id.index]; }

    [[nodiscard]] int line(ExprId id) const { return expression_lines_[id.index]; }

    StatId add_statement(Stat stat, int line);

    [[nodiscard]] const Stat& statement(StatId id) const { return statements_[id.index]; }

    [[nodiscard]] int line(StatId id) const { return statement_lines_[id.index]; }

    // ---- Lists ----
    // Each copies `ids` to the end of its list vector and returns where they
    // went. A list is parsed into a temporary vector first (nested lists add
    // their own entries in between), then stored in one go, so every list
    // ends up contiguous.

    IdRange<StatId> add_statement_list(std::span<const StatId> ids);
    IdRange<ExprId> add_expression_list(std::span<const ExprId> ids);
    IdRange<FieldId> add_field_list(std::span<const TableField> fields);
    IdRange<LocalNameId> add_local_name_list(std::span<const LocalName> names);

    // The statements of a block, in order.
    [[nodiscard]] std::span<const StatId> statements(const Block& block) const;
    [[nodiscard]] std::span<const ExprId> expressions(IdRange<ExprId> range) const;
    [[nodiscard]] std::span<const TableField> fields(IdRange<FieldId> range) const;
    [[nodiscard]] std::span<const LocalName> local_names(IdRange<LocalNameId> range) const;

    // ---- Text ----

    // Copies `bytes` to the end of the text buffer and returns where they went.
    TextRange add_text(std::string_view bytes);

    // The bytes of a range. The view is invalidated by the next add_text().
    [[nodiscard]] std::string_view text(TextRange range) const;

private:
    Block root_;

    std::vector<Expr> expressions_;
    std::vector<int> expression_lines_;
    std::vector<Stat> statements_;
    std::vector<int> statement_lines_;

    std::vector<StatId> statement_lists_;
    std::vector<ExprId> expression_lists_;
    std::vector<TableField> field_lists_; // fields are small, so stored directly
    std::vector<LocalName> local_name_lists_;

    std::string text_;
};

} // namespace lua2
