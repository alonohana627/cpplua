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
                    for (const ExprId value : ast_.expressions(node.values)) {
                        expression(value, depth + 1);
                    }
                },
            },
            ast_.statement(id));
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
