#include "parser/ast_printer.hpp"

#include <format>
#include <string>
#include <variant>

#include "text_format.hpp"

namespace lua2 {

namespace {

// Writes one node per line, indented two spaces per level of depth.
//
// Each node type has its own print() overload. statement() and expression()
// dispatch to them with std::visit, so a node type without an overload is a
// compile error ("no matching function for call to print"), the same safety
// as listing every lambda, without one giant function.
class Printer {
public:
    explicit Printer(const Ast& ast) : ast_(ast) {}

    std::string print_chunk() {
        line(0, "Chunk");
        block(ast_.root(), 1);
        return std::move(out_);
    }

private:
    // ---- Dispatch ----

    void statement(StatId id, int depth) {
        std::visit([&](const auto& node) { print(node, depth); }, ast_.statement(id));
    }

    void expression(ExprId id, int depth) {
        std::visit([&](const auto& node) { print(node, depth); }, ast_.expression(id));
    }

    // ---- Helpers ----

    void line(int depth, std::string_view text) {
        out_.append(static_cast<std::size_t>(depth) * 2, ' ');
        out_ += text;
        out_ += '\n';
    }

    void block(const Block& block, int depth) {
        for (const StatId id : ast_.statements(block)) {
            statement(id, depth);
        }
    }

    // A loop body under a "Body" heading, so it cannot be confused with the
    // expressions printed before it.
    void body(const Block& body, int depth) {
        line(depth, "Body");
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

    // Function(a, b, ...)   with the body's statements below
    void function(FunctionId id, int depth) {
        const Function& node = ast_.function(id);
        std::string parameters = names(node.parameters);
        if (node.is_vararg) {
            parameters += parameters.empty() ? "..." : ", ...";
        }
        line(depth, "Function(" + parameters + ")");
        block(node.body, depth + 1);
    }

    //   Item           a positional field, its value below
    //   Field name     a named field, its value below
    //   Field          a [key] = value field, the key then the value below
    void table_field(const TableField& field, int depth) {
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
        line(depth + 1, "Targets");
        expressions(node.targets, depth + 2);
        line(depth + 1, "Values");
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
        line(depth + 1, "Until");
        expression(node.condition, depth + 2);
    }

    void print(const IfStat& node, int depth) {
        line(depth, "If");
        bool first = true;
        for (const IfClause& clause : ast_.if_clauses(node.clauses)) {
            line(depth + 1, first ? "If" : "ElseIf");
            first = false;
            expression(clause.condition, depth + 2);
            line(depth + 2, "Then");
            block(clause.body, depth + 3);
        }
        if (node.has_else) {
            line(depth + 1, "Else");
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
        line(depth + 1, "In");
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

    void print(const NameExpr& node, int depth) {
        line(depth, std::format("Name {}", ast_.text(node.name)));
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

    void print(const FunctionExpr& node, int depth) { function(node.function, depth); }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during print
    const Ast& ast_;
    std::string out_;
};

} // namespace

std::string print_ast(const Ast& ast) {
    return Printer{ast}.print_chunk();
}

} // namespace lua2
