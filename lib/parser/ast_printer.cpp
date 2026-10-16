#include "parser/ast_printer.hpp"

#include <format>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "text_format.hpp"

namespace lua2 {

namespace {

// Writes one node per line, indented two spaces per level of depth.
//
// Each node type has its own print() overload, dispatched with std::visit,
// so a node type without an overload is a compile error.
//
// The printer does not recurse: a tree can be far deeper than the C++ stack
// allows (`1 + 1 + ... + 1` with ten thousand terms is ten thousand levels
// deep on its left side). Instead, print() writes the node's own line and
// *schedules* everything below it; run() then works through a stack of
// scheduled items in order.
class Printer {
public:
    explicit Printer(const Ast& ast) : ast_(ast) {}

    std::string print_chunk() {
        line(0, "Chunk");
        block(ast_.root(), 1);
        run();
        return std::move(out_);
    }

private:
    // ---- Scheduled work ----

    struct Heading {
        std::string_view text; // always a string literal
    };

    struct Work {
        std::variant<StatId, ExprId, FunctionId, TableField, Heading> item;
        int depth;
    };

    void run() {
        flush_scheduled();
        while (!stack_.empty()) {
            const Work work = stack_.back();
            stack_.pop_back();
            std::visit([&](const auto& item) { emit(item, work.depth); }, work.item);
            flush_scheduled();
        }
    }

    // Moves the items scheduled by the last print() onto the stack, last
    // first, so they come off it in the order they were scheduled.
    void flush_scheduled() {
        for (const Work& work : scheduled_ | std::views::reverse) {
            stack_.push_back(work);
        }
        scheduled_.clear();
    }

    void emit(StatId id, int depth) {
        std::visit([&](const auto& node) { print(node, depth); }, ast_.statement(id));
    }

    void emit(ExprId id, int depth) {
        std::visit([&](const auto& node) { print(node, depth); }, ast_.expression(id));
    }

    void emit(FunctionId id, int depth) { print_function(ast_.function(id), depth); }

    void emit(const TableField& field, int depth) { print_table_field(field, depth); }

    void emit(Heading heading, int depth) { line(depth, heading.text); }

    // ---- Helpers ----
    //
    // line() writes immediately, so print() uses it only for the node's own
    // first line. Everything after it goes through the schedulers below, to
    // keep the order right.

    void line(int depth, std::string_view text) {
        out_.append(static_cast<std::size_t>(depth) * 2, ' ');
        out_ += text;
        out_ += '\n';
    }

    void heading(int depth, std::string_view text) {
        scheduled_.push_back({.item = Heading{.text = text}, .depth = depth});
    }

    void statement(StatId id, int depth) { scheduled_.push_back({.item = id, .depth = depth}); }

    void expression(ExprId id, int depth) { scheduled_.push_back({.item = id, .depth = depth}); }

    void function(FunctionId id, int depth) { scheduled_.push_back({.item = id, .depth = depth}); }

    void table_field(const TableField& field, int depth) {
        scheduled_.push_back({.item = field, .depth = depth});
    }

    void block(const Block& block, int depth) {
        for (const StatId id : ast_.statements(block)) {
            statement(id, depth);
        }
    }

    // A loop body under a "Body" heading, so it cannot be confused with the
    // expressions printed before it.
    void body(const Block& body, int depth) {
        heading(depth, "Body");
        block(body, depth + 1);
    }

    void expressions(IdRange<ExprId> list, int depth) {
        for (const ExprId id : ast_.expressions(list)) {
            expression(id, depth);
        }
    }

    // "a, b <const>, c <close>"
    [[nodiscard]] std::string names(IdRange<LocalNameId> list) const {
        std::string out;
        for (const LocalName& name : ast_.local_names(list)) {
            out += out.empty() ? "" : ", ";
            out += ast_.text(name.name);
            switch (name.attribute) {
            case LocalName::Attribute::None: break;
            case LocalName::Attribute::Const: out += " <const>"; break;
            case LocalName::Attribute::Close: out += " <close>"; break;
            }
        }
        return out;
    }

    // Function(a, b, ...)                     with the body's statements below
    // Function(a) [upvalues: _ENV, count]     when it uses upvalues
    void print_function(const Function& node, int depth) {
        std::string text = "Function(" + names(node.parameters);
        if (node.is_vararg) {
            text += node.parameters.count == 0 ? "..." : ", ...";
        }
        text += ')';
        std::string upvalues;
        for (const Upvalue& upvalue : ast_.upvalues(node.upvalues)) {
            upvalues += upvalues.empty() ? "" : ", ";
            upvalues += ast_.text(upvalue.name);
        }
        if (!upvalues.empty()) {
            text += " [upvalues: " + upvalues + "]";
        }
        line(depth, text);
        block(node.body, depth + 1);
    }

    //   Item           a positional field, its value below
    //   Field name     a named field, its value below
    //   Field          a [key] = value field, the key then the value below
    void print_table_field(const TableField& field, int depth) {
        switch (field.kind) {
        case TableField::Kind::Positional:
            line(depth, "Item");
            expression(field.value, depth + 1);
            return;
        case TableField::Kind::Named: {
            const auto& key = std::get<StringExpr>(ast_.expression(field.key));
            line(depth, std::format("Field {}", ast_.text(key.text)));
            expression(field.value, depth + 1);
            return;
        }
        case TableField::Kind::Keyed:
            line(depth, "Field");
            expression(field.key, depth + 1);
            expression(field.value, depth + 1);
            return;
        }
    }

    // ---- Statements ----

    void print(const ReturnStat& node, int depth) {
        line(depth, "Return");
        expressions(node.values, depth + 1);
    }

    void print(const CallStat& node, int depth) {
        line(depth, "CallStatement");
        expression(node.call, depth + 1);
    }

    void print(const AssignStat& node, int depth) {
        line(depth, "Assign");
        heading(depth + 1, "Targets");
        expressions(node.targets, depth + 2);
        heading(depth + 1, "Values");
        expressions(node.values, depth + 2);
    }

    void print(const LocalStat& node, int depth) {
        line(depth, "Local " + names(node.names));
        expressions(node.values, depth + 1);
    }

    void print(const DoStat& node, int depth) {
        line(depth, "Do");
        block(node.body, depth + 1);
    }

    void print(const WhileStat& node, int depth) {
        line(depth, "While");
        expression(node.condition, depth + 1);
        body(node.body, depth + 1);
    }

    void print(const RepeatStat& node, int depth) {
        line(depth, "Repeat");
        body(node.body, depth + 1);
        heading(depth + 1, "Until");
        expression(node.condition, depth + 2);
    }

    void print(const IfStat& node, int depth) {
        line(depth, "If");
        bool first = true;
        for (const IfClause& clause : ast_.if_clauses(node.clauses)) {
            heading(depth + 1, first ? "If" : "ElseIf");
            first = false;
            expression(clause.condition, depth + 2);
            heading(depth + 2, "Then");
            block(clause.body, depth + 3);
        }
        if (node.has_else) {
            heading(depth + 1, "Else");
            block(node.else_body, depth + 2);
        }
    }

    void print(const NumericForStat& node, int depth) {
        line(depth, std::format("NumericFor {}", ast_.text(node.variable)));
        expression(node.start, depth + 1);
        expression(node.limit, depth + 1);
        if (node.has_step) {
            expression(node.step, depth + 1);
        }
        body(node.body, depth + 1);
    }

    void print(const GenericForStat& node, int depth) {
        line(depth, "GenericFor " + names(node.names));
        heading(depth + 1, "In");
        expressions(node.values, depth + 2);
        body(node.body, depth + 1);
    }

    void print(const FunctionStat& node, int depth) {
        line(depth, node.is_method ? "FunctionStatement (method)" : "FunctionStatement");
        expression(node.target, depth + 1);
        function(node.function, depth + 1);
    }

    void print(const LocalFunctionStat& node, int depth) {
        line(depth, std::format("LocalFunction {}", ast_.text(node.name)));
        function(node.function, depth + 1);
    }

    void print(const GotoStat& node, int depth) {
        line(depth, std::format("Goto {}", ast_.text(node.label)));
    }

    void print(const LabelStat& node, int depth) {
        line(depth, std::format("Label {}", ast_.text(node.name)));
    }

    void print(const BreakStat& /*node*/, int depth) { line(depth, "Break"); }

    // ---- Expressions ----

    void print(const NilExpr& /*node*/, int depth) { line(depth, "Nil"); }

    void print(const TrueExpr& /*node*/, int depth) { line(depth, "True"); }

    void print(const FalseExpr& /*node*/, int depth) { line(depth, "False"); }

    void print(const VarargExpr& /*node*/, int depth) { line(depth, "Vararg"); }

    void print(const IntegerExpr& node, int depth) {
        line(depth, std::format("Integer {}", node.value));
    }

    void print(const FloatExpr& node, int depth) {
        line(depth, "Float " + format_float(node.value));
    }

    void print(const StringExpr& node, int depth) {
        line(depth, "String " + quoted(ast_.text(node.text)));
    }

    void print(const UnaryExpr& node, int depth) {
        line(depth, std::format("Unary {}", operator_text(node.op)));
        expression(node.operand, depth + 1);
    }

    void print(const BinaryExpr& node, int depth) {
        line(depth, std::format("Binary {}", operator_text(node.op)));
        expression(node.left, depth + 1);
        expression(node.right, depth + 1);
    }

    // Name x               a global
    // Name x (local 2)     a local in slot 2
    // Name x (upvalue 0)   the function's upvalue 0
    void print(const NameExpr& node, int depth) {
        switch (node.variable.kind) {
        case VariableRef::Kind::Global:
            line(depth, std::format("Name {}", ast_.text(node.name)));
            return;
        case VariableRef::Kind::Local:
            line(depth,
                 std::format("Name {} (local {})", ast_.text(node.name), node.variable.index));
            return;
        case VariableRef::Kind::Upvalue:
            line(depth,
                 std::format("Name {} (upvalue {})", ast_.text(node.name), node.variable.index));
            return;
        }
    }

    void print(const ParenExpr& node, int depth) {
        line(depth, "Paren");
        expression(node.inner, depth + 1);
    }

    void print(const IndexExpr& node, int depth) {
        line(depth, "Index");
        expression(node.object, depth + 1);
        expression(node.key, depth + 1);
    }

    void print(const CallExpr& node, int depth) {
        line(depth, "Call"); // the function first, then the arguments
        expression(node.function, depth + 1);
        expressions(node.arguments, depth + 1);
    }

    void print(const MethodCallExpr& node, int depth) {
        line(depth, std::format("MethodCall {}", ast_.text(node.method)));
        expression(node.object, depth + 1);
        expressions(node.arguments, depth + 1);
    }

    void print(const TableExpr& node, int depth) {
        line(depth, "Table");
        for (const TableField& field : ast_.fields(node.fields)) {
            table_field(field, depth + 1);
        }
    }

    void print(const FunctionExpr& node, int depth) {
        print_function(ast_.function(node.function), depth);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during print
    const Ast& ast_;
    std::string out_;
    std::vector<Work> stack_;     // items still to print, the next one last
    std::vector<Work> scheduled_; // items scheduled by the print() running now
};

} // namespace

std::string print_ast(const Ast& ast) {
    return Printer{ast}.print_chunk();
}

} // namespace lua2
