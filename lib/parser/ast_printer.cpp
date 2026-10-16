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

    // A loop body under a "Body" heading, so it cannot be confused with the
    // expressions printed before it.
    void body(const Block& body, int depth) {
        line(depth, "Body");
        block(body, depth + 1);
    }

    // Every alternative of Stat needs a lambda here, or this does not compile.
    void statement(StatId id, int depth) {
        std::visit(Overloaded{
                       [&](const ReturnStat& node) {
                           line(depth, "Return");
                           expressions(node.values, depth + 1);
                       },
                       [&](const CallStat& node) {
                           line(depth, "CallStatement");
                           expression(node.call, depth + 1);
                       },
                       [&](const AssignStat& node) {
                           line(depth, "Assign");
                           line(depth + 1, "Targets");
                           expressions(node.targets, depth + 2);
                           line(depth + 1, "Values");
                           expressions(node.values, depth + 2);
                       },
                       [&](const LocalStat& node) {
                           // Local a, b <const>, c <close>   with the values below
                           std::string names;
                           for (const LocalName& name : ast_.local_names(node.names)) {
                               names += names.empty() ? "" : ", ";
                               names += ast_.text(name.name);
                               switch (name.attribute) {
                               case LocalName::Attribute::None: break;
                               case LocalName::Attribute::Const: names += " <const>"; break;
                               case LocalName::Attribute::Close: names += " <close>"; break;
                               }
                           }
                           line(depth, "Local " + names);
                           expressions(node.values, depth + 1);
                       },
                       [&](const DoStat& node) {
                           line(depth, "Do");
                           block(node.body, depth + 1);
                       },
                       [&](const WhileStat& node) {
                           line(depth, "While");
                           expression(node.condition, depth + 1);
                           body(node.body, depth + 1);
                       },
                       [&](const RepeatStat& node) {
                           line(depth, "Repeat");
                           body(node.body, depth + 1);
                           line(depth + 1, "Until");
                           expression(node.condition, depth + 2);
                       },
                       [&](const IfStat& node) {
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
                       },
                       [&](const NumericForStat& node) {
                           line(depth, std::format("NumericFor {}", ast_.text(node.variable)));
                           expression(node.start, depth + 1);
                           expression(node.limit, depth + 1);
                           if (node.has_step) {
                               expression(node.step, depth + 1);
                           }
                           body(node.body, depth + 1);
                       },
                       [&](const GenericForStat& node) {
                           std::string names;
                           for (const LocalName& name : ast_.local_names(node.names)) {
                               names += names.empty() ? "" : ", ";
                               names += ast_.text(name.name);
                           }
                           line(depth, "GenericFor " + names);
                           line(depth + 1, "In");
                           expressions(node.values, depth + 2);
                           body(node.body, depth + 1);
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
