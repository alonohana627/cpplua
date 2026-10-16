#include "parser/ast_printer.hpp"

#include <format>
#include <string>
#include <variant>

#include "overloaded.hpp"
#include "text_format.hpp"

namespace lua2 {

namespace {

// Writes one node per line, indented two spaces per level of depth.
class Printer {
public:
    explicit Printer(const Ast& ast) : ast_(ast) {}

    std::string print() {
        line(0, "Chunk");
        block(ast_.root(), 1);
        return std::move(out_);
    }

private:
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

    // Every alternative of Stat needs a lambda here, or this does not compile.
    void statement(StatId id, int depth) {
        std::visit(
            Overloaded{
                [&](const ReturnStat& node) {
                    line(depth, "Return");
                    expressions(node.values, depth + 1);
                },
            },
            ast_.statement(id));
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

    void expressions(IdRange<ExprId> list, int depth) {
        for (const ExprId id : ast_.expressions(list)) {
            expression(id, depth);
        }
    }

    // Every alternative of Expr needs a lambda here, or this does not compile.
    void expression(ExprId id, int depth) {
        std::visit(
            Overloaded{
                [&](const NilExpr&) { line(depth, "Nil"); },
                [&](const TrueExpr&) { line(depth, "True"); },
                [&](const FalseExpr&) { line(depth, "False"); },
                [&](const VarargExpr&) { line(depth, "Vararg"); },
                [&](const IntegerExpr& node) {
                    line(depth, std::format("Integer {}", node.value));
                },
                [&](const FloatExpr& node) { line(depth, "Float " + format_float(node.value)); },
                [&](const StringExpr& node) {
                    line(depth, "String " + quoted(ast_.text(node.text)));
                },
                [&](const UnaryExpr& node) {
                    line(depth, std::format("Unary {}", operator_text(node.op)));
                    expression(node.operand, depth + 1);
                },
                [&](const BinaryExpr& node) {
                    line(depth, std::format("Binary {}", operator_text(node.op)));
                    expression(node.left, depth + 1);
                    expression(node.right, depth + 1);
                },
                [&](const NameExpr& node) {
                    line(depth, std::format("Name {}", ast_.text(node.name)));
                },
                [&](const ParenExpr& node) {
                    line(depth, "Paren");
                    expression(node.inner, depth + 1);
                },
                [&](const IndexExpr& node) {
                    line(depth, "Index");
                    expression(node.object, depth + 1);
                    expression(node.key, depth + 1);
                },
                [&](const CallExpr& node) {
                    line(depth, "Call"); // the function first, then the arguments
                    expression(node.function, depth + 1);
                    expressions(node.arguments, depth + 1);
                },
                [&](const MethodCallExpr& node) {
                    line(depth, std::format("MethodCall {}", ast_.text(node.method)));
                    expression(node.object, depth + 1);
                    expressions(node.arguments, depth + 1);
                },
                [&](const TableExpr& node) {
                    line(depth, "Table");
                    for (const TableField& field : ast_.fields(node.fields)) {
                        table_field(field, depth + 1);
                    }
                },
            },
            ast_.expression(id));
    }

    const Ast& ast_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during
                     // print()
    std::string out_;
};

} // namespace

std::string print_ast(const Ast& ast) {
    return Printer{ast}.print();
}

} // namespace lua2
