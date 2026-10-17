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

struct IfClauseTag {};

struct FunctionTag {};

struct UpvalueTag {};

using ExprId = NodeId<ExprTag>;           // an expression node
using StatId = NodeId<StatTag>;           // a statement node
using FieldId = NodeId<FieldTag>;         // a field of a table constructor
using LocalNameId = NodeId<LocalNameTag>; // a name declared by `local`
using IfClauseId = NodeId<IfClauseTag>;   // an if or elseif clause
using FunctionId = NodeId<FunctionTag>;   // a function body
using UpvalueId = NodeId<UpvalueTag>;     // an upvalue of a function

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
// Where a variable lives, decided while parsing (G5.6).
struct VariableRef {
    enum class Kind : std::uint8_t {
        Global,  // a field of _ENV
        Local,   // a local of the current function; index is its slot
        Upvalue, // a local of an enclosing function; index is in the function's upvalue list
    };

    Kind kind = Kind::Global;
    std::uint32_t index = 0; // unused for Global

    bool operator==(const VariableRef&) const = default;
};

struct NameExpr {
    TextRange name;
    VariableRef variable;
    // For a global: where _ENV is, since `x` means `_ENV.x`. Usually an
    // upvalue, but `local _ENV = t` makes it a local.
    VariableRef environment;

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
    int method_line = 0; // where the method name is

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

// `function funcbody`: a function value (G5.1). The body itself is a
// Function in the Ast's function vector.
struct FunctionExpr {
    FunctionId function;

    bool operator==(const FunctionExpr&) const = default;
};

using Expr = std::variant<NilExpr, TrueExpr, FalseExpr, IntegerExpr, FloatExpr, StringExpr,
                          VarargExpr, UnaryExpr, BinaryExpr, NameExpr, ParenExpr, IndexExpr,
                          CallExpr, MethodCallExpr, TableExpr, FunctionExpr>;

// ---- Statements (G3.1) ----

// A list of statements (G2.2): the body of a chunk, function, loop or branch.
struct Block {
    IdRange<StatId> statements;

    bool operator==(const Block&) const = default;
};

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

// `do block end`
struct DoStat {
    Block body;

    bool operator==(const DoStat&) const = default;
};

// `while exp do block end`
struct WhileStat {
    ExprId condition;
    Block body;

    bool operator==(const WhileStat&) const = default;
};

// `repeat block until exp`. The condition can see the body's locals (G3.14).
struct RepeatStat {
    Block body;
    ExprId condition;

    bool operator==(const RepeatStat&) const = default;
};

// One `if exp then block` or `elseif exp then block` of an IfStat.
struct IfClause {
    ExprId condition;
    Block body;
    int then_line = 0; // where `then` is (its test is compiled there)

    bool operator==(const IfClause&) const = default;
};

// `if exp then block { elseif exp then block } [ else block ] end`. The
// first clause is the `if`, the others are the `elseif`s, in order.
struct IfStat {
    IdRange<IfClauseId> clauses;
    Block else_body;
    bool has_else = false;

    bool operator==(const IfStat&) const = default;
};

// `for Name = exp, exp [ , exp ] do block end` (G3.10)
struct NumericForStat {
    TextRange variable;
    ExprId start;
    ExprId limit;
    ExprId step; // only meaningful when has_step
    bool has_step = false;
    Block body;

    bool operator==(const NumericForStat&) const = default;
};

// `for namelist in explist do block end`
struct GenericForStat {
    IdRange<LocalNameId> names; // all without attributes
    IdRange<ExprId> values;
    Block body;

    bool operator==(const GenericForStat&) const = default;
};

// `function funcname funcbody` (G3.5, G3.11). The target is what gets
// assigned: a NameExpr for `function f`, or an IndexExpr chain for
// `function a.b.c`. With `:` (is_method), the last name is the method and the
// function has an extra first parameter named `self`.
struct FunctionStat {
    ExprId target;
    FunctionId function;
    bool is_method = false;

    bool operator==(const FunctionStat&) const = default;
};

// `local function Name funcbody` (G3.12). The local exists inside its own
// body, so the function can call itself.
struct LocalFunctionStat {
    TextRange name;
    FunctionId function;

    bool operator==(const LocalFunctionStat&) const = default;
};

// `goto Name` (G8.2). Whether the label exists and is visible is checked in
// part 9.
struct GotoStat {
    TextRange label;

    bool operator==(const GotoStat&) const = default;
};

// `::Name::` (G3.4)
struct LabelStat {
    TextRange name;

    bool operator==(const LabelStat&) const = default;
};

// `break` (G8.6): leaves the innermost loop. Unlike `return`, it may appear
// anywhere in a block (G8.7).
struct BreakStat {
    bool operator==(const BreakStat&) const = default;
};

using Stat = std::variant<ReturnStat, CallStat, AssignStat, LocalStat, DoStat, WhileStat,
                          RepeatStat, IfStat, NumericForStat, GenericForStat, FunctionStat,
                          LocalFunctionStat, GotoStat, LabelStat, BreakStat>;

// A function body: `( parlist ) block end` (G5.2, G5.3). Shared by function
// expressions, function statements and local functions.
// A local of an enclosing function that a function uses, captured when the
// closure is created (G5.6).
struct UpvalueDesc {
    TextRange name;
    // Where the enclosing function holds it: its local slot `index`, or (when
    // it is itself an upvalue there) its upvalue `index`.
    bool from_parent_local = false;
    std::uint32_t index = 0;

    bool operator==(const UpvalueDesc&) const = default;
};

struct Function {
    IdRange<LocalNameId> parameters; // in order; `self` first for methods
    bool is_vararg = false;          // the parameter list ends with `...`
    int vararg_line = 0;             // where that `...` is
    IdRange<UpvalueId> upvalues;     // in order of first use; the main chunk's is _ENV
    Block body;
    int line = 0;     // where `function` is
    int end_line = 0; // where the closing `end` is; for the main chunk, its last token

    bool operator==(const Function&) const = default;
};

// Nodes hold only numbers and ids, never strings or pointers, so copying one
// is a plain memory copy and the vectors never run destructors. Keep it so.
static_assert(std::is_trivially_copyable_v<Expr>);
static_assert(std::is_trivially_copyable_v<Stat>);
static_assert(std::is_trivially_copyable_v<Function>);

class Ast {
public:
    // The chunk is compiled as a function (G2.4): no parameters, vararg, and
    // _ENV as its only upvalue.
    [[nodiscard]] FunctionId main_function() const noexcept { return main_; }

    void set_main_function(FunctionId id) noexcept { main_ = id; }

    // The block of the whole chunk (G2.1).
    [[nodiscard]] const Block& root() const { return function(main_).body; }

    // ---- Nodes ----
    // Every node records its line, kept in a parallel vector (structure of
    // arrays) so the node variants stay small: the line it starts on (for a
    // binary or unary operator, the operator's line; for a call, the line of
    // its arguments), and the line of its last token, which the compiler
    // needs to give each instruction Lua's line (stage 5).

    ExprId add_expression(Expr expr, int line, int end_line = 0);

    [[nodiscard]] const Expr& expression(ExprId id) const { return expressions_[id.index]; }

    [[nodiscard]] int line(ExprId id) const { return expression_lines_[id.index]; }

    [[nodiscard]] int end_line(ExprId id) const { return expression_end_lines_[id.index]; }

    StatId add_statement(Stat stat, int line, int end_line = 0);

    [[nodiscard]] const Stat& statement(StatId id) const { return statements_[id.index]; }

    [[nodiscard]] int line(StatId id) const { return statement_lines_[id.index]; }

    [[nodiscard]] int end_line(StatId id) const { return statement_end_lines_[id.index]; }

    // Function bodies record their own lines (Function::line, end_line).
    FunctionId add_function(const Function& function);

    [[nodiscard]] const Function& function(FunctionId id) const { return functions_[id.index]; }

    // ---- Lists ----
    // Each copies `ids` to the end of its list vector and returns where they
    // went. A list is parsed into a temporary vector first (nested lists add
    // their own entries in between), then stored in one go, so every list
    // ends up contiguous.

    IdRange<StatId> add_statement_list(std::span<const StatId> ids);
    IdRange<ExprId> add_expression_list(std::span<const ExprId> ids);
    IdRange<FieldId> add_field_list(std::span<const TableField> fields);
    IdRange<LocalNameId> add_local_name_list(std::span<const LocalName> names);
    IdRange<IfClauseId> add_if_clause_list(std::span<const IfClause> clauses);
    IdRange<UpvalueId> add_upvalue_list(std::span<const UpvalueDesc> upvalues);

    // The statements of a block, in order.
    [[nodiscard]] std::span<const StatId> statements(const Block& block) const;
    [[nodiscard]] std::span<const ExprId> expressions(IdRange<ExprId> range) const;
    [[nodiscard]] std::span<const TableField> fields(IdRange<FieldId> range) const;
    [[nodiscard]] std::span<const LocalName> local_names(IdRange<LocalNameId> range) const;
    [[nodiscard]] std::span<const IfClause> if_clauses(IdRange<IfClauseId> range) const;
    [[nodiscard]] std::span<const UpvalueDesc> upvalues(IdRange<UpvalueId> range) const;

    // ---- Text ----

    // Copies `bytes` to the end of the text buffer and returns where they went.
    TextRange add_text(std::string_view bytes);

    // The bytes of a range. The view is invalidated by the next add_text().
    [[nodiscard]] std::string_view text(TextRange range) const;

private:
    FunctionId main_;

    std::vector<Expr> expressions_;
    std::vector<int> expression_lines_;
    std::vector<int> expression_end_lines_;
    std::vector<Stat> statements_;
    std::vector<int> statement_lines_;
    std::vector<int> statement_end_lines_;
    std::vector<Function> functions_;

    std::vector<StatId> statement_lists_;
    std::vector<ExprId> expression_lists_;
    std::vector<TableField> field_lists_; // fields are small, so stored directly
    std::vector<LocalName> local_name_lists_;
    std::vector<IfClause> if_clause_lists_;
    std::vector<UpvalueDesc> upvalue_lists_;

    std::string text_;
};

} // namespace lua2
