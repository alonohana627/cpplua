#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "lexer/token.hpp"
#include "overloaded.hpp"
#include "parser/ast.hpp"
#include "parser/ast_printer.hpp"
#include "parser/parser.hpp"
#include "text_format.hpp"

using cpplua::Parser;

namespace {

// Parses `source` and returns the printed tree, failing the test on an error.
std::string ast_of(std::string_view source) {
    Parser parser{source, "input"};
    auto ast = parser.parse_chunk();
    if (!ast) {
        FAIL("unexpected parse error: " << ast.error().message);
    }
    return cpplua::print_ast(*ast);
}

// Parses `source`, which must fail, and returns the error message.
std::string parse_error(std::string_view source) {
    Parser parser{source, "input"};
    auto ast = parser.parse_chunk();
    REQUIRE_FALSE(ast.has_value());
    return ast.error().message;
}

} // namespace

// ---- Part 0: foundations ----

TEST_CASE("G2.1 an empty chunk is a Chunk with no statements", "[parser]") {
    const auto source = GENERATE(as<std::string_view>{}, "", "   ", "\n\n", "-- a comment",
                                 "--[[ a\nlong comment ]]");
    CAPTURE(source);

    CHECK(ast_of(source) == "Chunk\n");
}

TEST_CASE("G2.5 anything left after the chunk is '<eof> expected'", "[parser]") {
    // A word that ends a block cannot start the chunk, and nothing may come
    // after a return. Note that <eof> is not quoted (G9.3).
    CHECK(parse_error("return; x") == "input:1: <eof> expected near 'x'");
    CHECK(parse_error("\n\n  end") == "input:3: <eof> expected near 'end'");
}

TEST_CASE("G9.2 near shows the token as written in the source", "[parser]") {
    const auto [source, near] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"0x10", "'0x10'"},   // not '16'
        {"1e3", "'1e3'"},     // not '1000.0'
        {"local", "'local'"}, // keywords quoted
        {"==", "'=='"},       // symbols quoted
        {"...", "'...'"},     //
        {"[[x]]", "'[[x]]'"}, // a long string as written, brackets included
        {"[==[a\nb]==]", "'[==[a\nb]==]'"},
        {R"("q")", R"('"q"')"},        // a short string with its own quotes
        {R"('q')", R"(''q'')"},        //
        {R"("\65\x42")", R"('"AB"')"}, // a short string after escapes, like Lua
        {"@", "'@'"},                  // a stray byte
        {"\x80", R"('<\128>')"},       // an unprintable stray byte
    }));
    CAPTURE(source);

    // After `return;` the block is over, so any token is left over and shown.
    CHECK(parse_error("return; " + std::string(source)) ==
          "input:1: <eof> expected near " + std::string(near));
}

TEST_CASE("G9.4 a lexer error stops the parser with the lexer's message", "[parser]") {
    CHECK(parse_error("\"abc") == "input:1: unfinished string near <eof>");
    CHECK(parse_error("\n3x") == "input:2: malformed number near '3x'");
}

TEST_CASE("G9.1 the error records where in the parser it was made", "[parser]") {
    Parser parser{"x"};
    const auto ast = parser.parse_chunk();

    REQUIRE_FALSE(ast.has_value());
    CHECK(std::string_view{ast.error().origin.file_name()}.ends_with("parser.cpp"));
}

// ---- 'X' expected names (G9.3) ----

TEST_CASE("G9.3 how token kinds are named in 'X expected' messages", "[parser]") {
    using cpplua::expected_text;
    using cpplua::TokenKind;

    CHECK(expected_text(TokenKind::EndOfStream) == "<eof>");
    CHECK(expected_text(TokenKind::Name) == "<name>");
    CHECK(expected_text(TokenKind::String) == "<string>");
    CHECK(expected_text(TokenKind::End) == "'end'");
    CHECK(expected_text(TokenKind::Then) == "'then'");
    CHECK(expected_text(TokenKind::Assign) == "'='");
    CHECK(expected_text(TokenKind::RParen) == "')'");
    CHECK(expected_text(TokenKind::Ellipsis) == "'...'");
    CHECK(expected_text(TokenKind::DoubleColon) == "'::'");
}

// ---- The arena (lib/internal/parser/ast.hpp) ----

TEST_CASE("ExprId and StatId are different types", "[parser][ast]") {
    STATIC_CHECK_FALSE(std::is_convertible_v<cpplua::ExprId, cpplua::StatId>);
    STATIC_CHECK_FALSE(std::is_convertible_v<cpplua::StatId, cpplua::ExprId>);
    STATIC_CHECK(sizeof(cpplua::StatId) == 4);
}

TEST_CASE("statement lists are stored back to back", "[parser][ast]") {
    using cpplua::StatId;
    cpplua::Ast ast;

    const std::array first{StatId{.index = 7}, StatId{.index = 8}};
    const std::array second{StatId{.index = 9}};
    const auto first_range = ast.add_statement_list(first);
    const auto second_range = ast.add_statement_list(second);
    const auto empty_range = ast.add_statement_list({});

    CHECK(first_range == cpplua::IdRange<StatId>{.first = 0, .count = 2});
    CHECK(second_range == cpplua::IdRange<StatId>{.first = 2, .count = 1});
    CHECK(empty_range.count == 0);

    const auto read_back = ast.statements(cpplua::Block{.statements = first_range});
    REQUIRE(read_back.size() == 2);
    CHECK(read_back[0] == StatId{.index = 7});
    CHECK(read_back[1] == StatId{.index = 8});
}

// ---- Part 1: simple expressions and return ----

TEST_CASE("G4.1 every simple expression", "[parser]") {
    const auto [source, node] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"return nil", "Nil"},
        {"return true", "True"},
        {"return false", "False"},
        {"return ...", "Vararg"},
        {"return 42", "Integer 42"},
        {"return 0x10", "Integer 16"},
        {"return 0xffffffffffffffff", "Integer -1"},
        {"return 2.5", "Float 2.5"},
        {"return 1e3", "Float 1000.0"},
        {"return 1e400", "Float inf"},
        {"return 'hi'", R"(String "hi")"},
        {R"(return "a\tb\0")", R"(String "a\tb\x00")"},
        {"return [[long\nstring]]", R"(String "long\nstring")"},
        {"return ''", R"(String "")"},
    }));
    CAPTURE(source);

    CHECK(ast_of(source) == "Chunk\n  Return\n    " + std::string(node) + "\n");
}

TEST_CASE("G2.3 return with no values", "[parser]") {
    CHECK(ast_of("return") == "Chunk\n  Return\n");
    CHECK(ast_of("return;") == "Chunk\n  Return\n");
    CHECK(ast_of("  return  -- done") == "Chunk\n  Return\n");
}

TEST_CASE("G4.6 return with a list of values", "[parser]") {
    CHECK(ast_of("return 1, 'two', nil, ...") == "Chunk\n"
                                                 "  Return\n"
                                                 "    Integer 1\n"
                                                 "    String \"two\"\n"
                                                 "    Nil\n"
                                                 "    Vararg\n");
    CHECK(ast_of("return 1,\n2,\n3;") == "Chunk\n"
                                         "  Return\n"
                                         "    Integer 1\n"
                                         "    Integer 2\n"
                                         "    Integer 3\n");
}

TEST_CASE("G2.5 return must be the last statement", "[parser]") {
    CHECK(parse_error("return 1 return 2") == "input:1: <eof> expected near 'return'");
    CHECK(parse_error("return 1\nx") == "input:2: <eof> expected near 'x'");
    CHECK(parse_error("return;;") == "input:1: <eof> expected near ';'"); // only one ';'
    CHECK(parse_error("return end") == "input:1: <eof> expected near 'end'");
}

TEST_CASE("G4.7 a token that cannot start an expression", "[parser]") {
    CHECK(parse_error("return ,") == "input:1: unexpected symbol near ','");
    CHECK(parse_error("return 1,") == "input:1: unexpected symbol near <eof>");
    CHECK(parse_error("return 1, , 2") == "input:1: unexpected symbol near ','");
    CHECK(parse_error("return 1,\n\n)") == "input:3: unexpected symbol near ')'");
}

TEST_CASE("G4.1 nodes record their line", "[parser][ast]") {
    Parser parser{"\nreturn\n1,\n\n'x'"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto statements = ast->statements(ast->root());
    REQUIRE(statements.size() == 1);
    CHECK(ast->line(statements[0]) == 2);

    const auto& node = std::get<cpplua::ReturnStat>(ast->statement(statements[0]));
    const auto values = ast->expressions(node.values);
    REQUIRE(values.size() == 2);
    CHECK(ast->line(values[0]) == 3);
    CHECK(ast->line(values[1]) == 5);
    CHECK(std::get<cpplua::IntegerExpr>(ast->expression(values[0])).value == 1);
    CHECK(ast->text(std::get<cpplua::StringExpr>(ast->expression(values[1])).text) == "x");
}

TEST_CASE("string values are stored once in the shared text buffer", "[parser][ast]") {
    cpplua::Ast ast;
    const auto first = ast.add_text("hello");
    const auto second = ast.add_text("");
    const auto third = ast.add_text("world");

    CHECK(first == cpplua::TextRange{.first = 0, .length = 5});
    CHECK(second.length == 0);
    CHECK(third == cpplua::TextRange{.first = 5, .length = 5});
    CHECK(ast.text(first) == "hello");
    CHECK(ast.text(third) == "world");
}

// ---- Part 2: operators and precedence ----

namespace {

std::string grouped_list(const cpplua::Ast& ast, cpplua::IdRange<cpplua::ExprId> list);

// Parses `source` and returns its single statement, failing the test on an
// error or on any other number of statements.
cpplua::Stat only_statement(std::string_view source, cpplua::Ast& ast) {
    Parser parser{source, "input"};
    auto parsed = parser.parse_chunk();
    if (!parsed) {
        FAIL("unexpected parse error: " << parsed.error().message);
    }
    ast = std::move(*parsed);
    const auto statements = ast.statements(ast.root());
    REQUIRE(statements.size() == 1);
    return ast.statement(statements[0]);
}

// Writes an expression with every operator in parentheses, so precedence is
// visible on one line: "1 + 2 * 3" becomes "(1 + (2 * 3))". Indexing shows as
// object{key} and Lua's own parentheses as [ ], so they cannot be confused
// with the grouping.
std::string grouped(const cpplua::Ast& ast, cpplua::ExprId id) {
    return std::visit(
        cpplua::Overloaded{
            [](const cpplua::NilExpr&) -> std::string { return "nil"; },
            [](const cpplua::TrueExpr&) -> std::string { return "true"; },
            [](const cpplua::FalseExpr&) -> std::string { return "false"; },
            [](const cpplua::VarargExpr&) -> std::string { return "..."; },
            [](const cpplua::IntegerExpr& node) { return std::to_string(node.value); },
            [](const cpplua::FloatExpr& node) { return cpplua::format_float(node.value); },
            [&](const cpplua::StringExpr& node) {
                return "'" + std::string(ast.text(node.text)) + "'";
            },
            [&](const cpplua::UnaryExpr& node) {
                const auto op = cpplua::operator_text(node.op);
                const std::string_view separator = op == "not" ? " " : "";
                return "(" + std::string(op) + separator + grouped(ast, node.operand) + ")";
            },
            [&](const cpplua::BinaryExpr& node) {
                return "(" + grouped(ast, node.left) + " " +
                       std::string(cpplua::operator_text(node.op)) + " " +
                       grouped(ast, node.right) + ")";
            },
            [&](const cpplua::NameExpr& node) { return std::string(ast.text(node.name)); },
            // Lua's own parentheses are shown as [ ] to tell them from the grouping.
            [&](const cpplua::ParenExpr& node) { return "[" + grouped(ast, node.inner) + "]"; },
            [&](const cpplua::IndexExpr& node) {
                return grouped(ast, node.object) + "{" + grouped(ast, node.key) + "}";
            },
            [&](const cpplua::CallExpr& node) {
                return grouped(ast, node.function) + "(" + grouped_list(ast, node.arguments) + ")";
            },
            [&](const cpplua::MethodCallExpr& node) {
                return grouped(ast, node.object) + ":" + std::string(ast.text(node.method)) + "(" +
                       grouped_list(ast, node.arguments) + ")";
            },
            [](const cpplua::FunctionExpr&) -> std::string { return "function"; },
            // Tables as {item, name=value, [key]=value}.
            [&](const cpplua::TableExpr& node) {
                std::string out = "{";
                for (const auto& field : ast.fields(node.fields)) {
                    out += out.size() == 1 ? "" : ", ";
                    switch (field.kind) {
                    case cpplua::TableField::Kind::Positional: break;
                    case cpplua::TableField::Kind::Named:
                        out += std::string(ast.text(
                                   std::get<cpplua::StringExpr>(ast.expression(field.key)).text)) +
                               "=";
                        break;
                    case cpplua::TableField::Kind::Keyed:
                        out += "[" + grouped(ast, field.key) + "]=";
                        break;
                    }
                    out += grouped(ast, field.value);
                }
                return out + "}";
            },
        },
        ast.expression(id));
}

std::string grouped_list(const cpplua::Ast& ast, cpplua::IdRange<cpplua::ExprId> list) {
    std::string out;
    for (const auto id : ast.expressions(list)) {
        out += out.empty() ? "" : ", ";
        out += grouped(ast, id);
    }
    return out;
}

// Parses `return <expression>` and returns the expression, grouped.
std::string group(std::string_view expression) {
    const std::string source = "return " + std::string(expression);
    Parser parser{source, "input"};
    const auto ast = parser.parse_chunk();
    if (!ast) {
        FAIL("unexpected parse error: " << ast.error().message);
    }
    const auto statement = ast->statements(ast->root())[0];
    const auto values =
        ast->expressions(std::get<cpplua::ReturnStat>(ast->statement(statement)).values);
    return grouped(*ast, values[0]);
}

} // namespace

TEST_CASE("G7.1 G7.2 every operator", "[parser][operator]") {
    const auto op =
        GENERATE(as<std::string_view>{}, "+", "-", "*", "/", "//", "%", "^", "..", "==", "~=", "<",
                 "<=", ">", ">=", "and", "or", "&", "|", "~", "<<", ">>");
    CAPTURE(op);
    const std::string source = "1 " + std::string(op) + " 2";

    CHECK(group(source) == "(" + source + ")");
}

TEST_CASE("G7.2 every unary operator", "[parser][operator]") {
    CHECK(group("-1") == "(-1)");
    CHECK(group("not true") == "(not true)");
    CHECK(group("#'abc'") == "(#'abc')");
    CHECK(group("~0") == "(~0)");
}

TEST_CASE("G7.3 each precedence level binds tighter than the one below", "[parser][operator]") {
    const auto [source, expected] = GENERATE(values<std::pair<std::string_view, std::string_view>>({
        {"1 or 2 and 3", "(1 or (2 and 3))"},
        {"1 and 2 < 3", "(1 and (2 < 3))"},
        {"1 < 2 | 3", "(1 < (2 | 3))"},
        {"1 | 2 ~ 3", "(1 | (2 ~ 3))"},
        {"1 ~ 2 & 3", "(1 ~ (2 & 3))"},
        {"1 & 2 << 3", "(1 & (2 << 3))"},
        {"1 << 2 .. 3", "(1 << (2 .. 3))"},
        {"1 .. 2 + 3", "(1 .. (2 + 3))"},
        {"1 + 2 * 3", "(1 + (2 * 3))"},
        {"-1 * 2", "((-1) * 2)"}, // unary above *
        {"-2 ^ 2", "(-(2 ^ 2))"}, // ^ above unary
        // The same pairs written the other way round.
        {"1 and 2 or 3", "((1 and 2) or 3)"},
        {"1 < 2 and 3", "((1 < 2) and 3)"},
        {"1 * 2 + 3", "((1 * 2) + 3)"},
        {"1 + 2 .. 3", "((1 + 2) .. 3)"},
    }));
    CAPTURE(source);

    CHECK(group(source) == expected);
}

TEST_CASE("G7.3 operators on the same level", "[parser][operator]") {
    CHECK(group("1 == 2 ~= 3 < 4") == "(((1 == 2) ~= 3) < 4)");
    CHECK(group("1 + 2 - 3") == "((1 + 2) - 3)");
    CHECK(group("1 * 2 / 3 // 4 % 5") == "((((1 * 2) / 3) // 4) % 5)");
    CHECK(group("1 << 2 >> 3") == "((1 << 2) >> 3)");
}

TEST_CASE("G7.5 left and right associativity", "[parser][operator]") {
    CHECK(group("1 - 2 - 3") == "((1 - 2) - 3)");
    CHECK(group("'a' .. 'b' .. 'c'") == "('a' .. ('b' .. 'c'))");
    CHECK(group("2 ^ 3 ^ 2") == "(2 ^ (3 ^ 2))");
    CHECK(group("1 or 2 or 3") == "((1 or 2) or 3)");
}

TEST_CASE("G7.4 ^ against unary operators", "[parser][operator]") {
    CHECK(group("-2 ^ 2") == "(-(2 ^ 2))");
    CHECK(group("2 ^ -3") == "(2 ^ (-3))");
    CHECK(group("2 ^ -3 ^ 2") == "(2 ^ (-(3 ^ 2)))");
    CHECK(group("not 1 == 2") == "((not 1) == 2)");
    CHECK(group("#'s' + 1") == "((#'s') + 1)");
}

TEST_CASE("G7.6 comparisons chain syntactically", "[parser][operator]") {
    CHECK(group("1 < 2 < 3") == "((1 < 2) < 3)");
}

TEST_CASE("G7.7 unary operators nest", "[parser][operator]") {
    CHECK(group("not not true") == "(not (not true))");
    CHECK(group("- -1") == "(-(-1))");
    CHECK(group("~~0") == "(~(~0))");
    CHECK(group("#-1") == "(#(-1))");
    CHECK(group("-#'ab'") == "(-(#'ab'))");
}

TEST_CASE("G7.7 two minus signs are a comment", "[parser][operator]") {
    CHECK(ast_of("return --1") == "Chunk\n  Return\n");
    CHECK(group("1 - -1") == "(1 - (-1))");
}

TEST_CASE("G7.8 a long mixed expression", "[parser][operator]") {
    CHECK(group("1 + 2 * 3 ^ 2 .. 'x' == 'y' and not false or nil") ==
          "(((((1 + (2 * (3 ^ 2))) .. 'x') == 'y') and (not false)) or nil)");
}

TEST_CASE("G4.7 an operator needs operands", "[parser][operator]") {
    CHECK(parse_error("return 1 +") == "input:1: unexpected symbol near <eof>");
    CHECK(parse_error("return * 2") == "input:1: unexpected symbol near '*'");
    CHECK(parse_error("return 1 + + 2") == "input:1: unexpected symbol near '+'");
    CHECK(parse_error("return not") == "input:1: unexpected symbol near <eof>");
    CHECK(parse_error("return 1 and\n\nor 2") == "input:3: unexpected symbol near 'or'");
}

TEST_CASE("G7.8 the tree of an operator expression", "[parser][operator]") {
    CHECK(ast_of("return -1 + 2") == "Chunk\n"
                                     "  Return\n"
                                     "    Binary +\n"
                                     "      Unary -\n"
                                     "        Integer 1\n"
                                     "      Integer 2\n");
}

TEST_CASE("G7.8 an operator node records its operator's line", "[parser][operator]") {
    Parser parser{"return 1\n+\n2"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto statement = ast->statements(ast->root())[0];
    const auto values =
        ast->expressions(std::get<cpplua::ReturnStat>(ast->statement(statement)).values);
    CHECK(ast->line(values[0]) == 2);
}

// ---- Part 3: names, fields, indexing and calls ----

TEST_CASE("G4.3 names", "[parser][suffix]") {
    CHECK(group("x") == "x");
    CHECK(group("_ENV") == "_ENV");
    CHECK(group("x + y * z") == "(x + (y * z))");
    CHECK(group("-x ^ 2") == "(-(x ^ 2))");
}

TEST_CASE("G4.3 fields and indexing", "[parser][suffix]") {
    CHECK(group("a.b") == "a{'b'}"); // a.b is a["b"]
    CHECK(group("a[1]") == "a{1}");
    CHECK(group("a[b + 1]") == "a{(b + 1)}");
    CHECK(group("a.b.c") == "a{'b'}{'c'}");
    CHECK(group("a[b][c]") == "a{b}{c}");
    CHECK(group("a[b[c]]") == "a{b{c}}");
}

TEST_CASE("G4.4 G4.5 calls", "[parser][suffix]") {
    CHECK(group("f()") == "f()");
    CHECK(group("f(1)") == "f(1)");
    CHECK(group("f(1, x, 'a')") == "f(1, x, 'a')");
    CHECK(group("f(g(1))") == "f(g(1))");
    CHECK(group("f()()") == "f()()");
    CHECK(group("f(1 + 2, ...)") == "f((1 + 2), ...)");
}

TEST_CASE("G4.10 string arguments", "[parser][suffix]") {
    CHECK(group("f'x'") == "f('x')");
    CHECK(group("f\"x\"") == "f('x')");
    CHECK(group("f[[x]]") == "f('x')");
    CHECK(group("require 'mod'") == "require('mod')");
    CHECK(group("f'a''b'") == "f('a')('b')"); // each string is another call
}

TEST_CASE("G4.9 method calls", "[parser][suffix]") {
    CHECK(group("obj:m()") == "obj:m()");
    CHECK(group("obj:m(1, 2)") == "obj:m(1, 2)");
    CHECK(group("obj:m'x'") == "obj:m('x')");
    CHECK(group("a.b:c(d)") == "a{'b'}:c(d)");
}

TEST_CASE("G4.8 suffixes chain left to right", "[parser][suffix]") {
    CHECK(group("a.b[c](d):e'f'") == "a{'b'}{c}(d):e('f')");
    CHECK(group("f().x") == "f(){'x'}");
    CHECK(group("t[1]:m()[2]") == "t{1}:m(){2}");
}

TEST_CASE("G4.8 suffixes bind tighter than every operator", "[parser][suffix]") {
    CHECK(group("-f(x)") == "(-f(x))");
    CHECK(group("#t.list") == "(#t{'list'})");
    CHECK(group("a.b ^ 2") == "(a{'b'} ^ 2)");
    CHECK(group("not obj:ok()") == "(not obj:ok())");
}

TEST_CASE("G4.11 parentheses are a node of their own", "[parser][suffix]") {
    CHECK(group("(1)") == "[1]");
    CHECK(group("(f())") == "[f()]");
    CHECK(group("(...)") == "[...]");
    CHECK(group("(1 + 2) * 3") == "([(1 + 2)] * 3)");
    CHECK(group("(f).x") == "[f]{'x'}");
    CHECK(group("('s'):upper()") == "['s']:upper()");
    CHECK(group("((x))") == "[[x]]");
}

TEST_CASE("G3.16 a call may continue on the next line", "[parser][suffix]") {
    CHECK(group("f\n(1)") == "f(1)");
    CHECK(group("a\n.b\n[c]\n:d()") == "a{'b'}{c}:d()");
}

TEST_CASE("G4.7 a literal cannot be called or indexed directly", "[parser][suffix]") {
    // Lua stops the expression after the string, then finds ':' left over.
    CHECK(parse_error("return 's':upper()") == "input:1: <eof> expected near ':'");
    CHECK(parse_error("return 1.x") == "input:1: malformed number near '1.x'");
}

TEST_CASE("G9.3 errors inside suffixes", "[parser][suffix]") {
    CHECK(parse_error("return a.") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("return a.1") == "input:1: <eof> expected near '.1'"); // .1 is a number
    CHECK(parse_error("return a.end") == "input:1: <name> expected near 'end'");
    CHECK(parse_error("return a[1") == "input:1: ']' expected near <eof>");
    CHECK(parse_error("return a[]") == "input:1: unexpected symbol near ']'");
    CHECK(parse_error("return obj:m") == "input:1: function arguments expected near <eof>");
    CHECK(parse_error("return obj:m.x") == "input:1: function arguments expected near '.'");
    CHECK(parse_error("return obj:1") == "input:1: <name> expected near '1'");
    CHECK(parse_error("return f(1") == "input:1: ')' expected near <eof>");
    CHECK(parse_error("return f(1,)") == "input:1: unexpected symbol near ')'");
    CHECK(parse_error("return ()") == "input:1: unexpected symbol near ')'");
    CHECK(parse_error("return (1") == "input:1: ')' expected near <eof>");
}

TEST_CASE("G9.3 a missing ')' names the '(' when it is on another line", "[parser][suffix]") {
    CHECK(parse_error("return f(1,\n2") ==
          "input:2: ')' expected (to close '(' at line 1) near <eof>");
    CHECK(parse_error("return (1\n+ 2\nx") ==
          "input:3: ')' expected (to close '(' at line 1) near 'x'");
    CHECK(parse_error("return (1 x") == "input:1: ')' expected near 'x'");
}

TEST_CASE("G4.4 the tree of calls and indexing", "[parser][suffix]") {
    CHECK(ast_of("return obj:m(1).x") == "Chunk\n"
                                         "  Return\n"
                                         "    Index\n"
                                         "      MethodCall m\n"
                                         "        Name obj\n"
                                         "        Integer 1\n"
                                         "      String \"x\"\n");
    CHECK(ast_of("return f(a, (b))") == "Chunk\n"
                                        "  Return\n"
                                        "    Call\n"
                                        "      Name f\n"
                                        "      Name a\n"
                                        "      Paren\n"
                                        "        Name b\n");
}

// ---- Part 4: table constructors ----

TEST_CASE("G6.1 an empty table", "[parser][table]") {
    CHECK(group("{}") == "{}");
    CHECK(group("{ }") == "{}");
}

TEST_CASE("G6.3 the three kinds of field", "[parser][table]") {
    CHECK(group("{1, 2, 3}") == "{1, 2, 3}");
    CHECK(group("{x = 1}") == "{x=1}");
    CHECK(group("{[1 + 1] = 'two'}") == "{[(1 + 1)]='two'}");
    CHECK(group("{1, x = 2, [3] = 4, 5}") == "{1, x=2, [3]=4, 5}");
}

TEST_CASE("G6.5 one token past the name decides", "[parser][table]") {
    CHECK(group("{x}") == "{x}");             // a positional field holding x
    CHECK(group("{x = y}") == "{x=y}");       // a named field
    CHECK(group("{x == y}") == "{(x == y)}"); // '==' is not '='
    CHECK(group("{x.y}") == "{x{'y'}}");      // '.' is not '=' either
}

TEST_CASE("G6.4 G6.7 separators", "[parser][table]") {
    CHECK(group("{1; 2, 3}") == "{1, 2, 3}");
    CHECK(group("{1,}") == "{1}");
    CHECK(group("{1;}") == "{1}");
    CHECK(group("{x = 1,\n y = 2,\n}") == "{x=1, y=2}");
}

TEST_CASE("G6.3 values are any expression", "[parser][table]") {
    CHECK(group("{f(), ..., {}, -x}") == "{f(), ..., {}, (-x)}");
    CHECK(group("{{1, 2}, {3}}") == "{{1, 2}, {3}}");
    CHECK(group("{[{}] = {}}") == "{[{}]={}}");
}

TEST_CASE("G6.1 a table is an operand", "[parser][table]") {
    CHECK(group("#{1, 2}") == "(#{1, 2})");
    CHECK(group("{} == {}") == "({} == {})");
    CHECK(group("({}).x") == "[{}]{'x'}");
}

TEST_CASE("G4.10 table arguments", "[parser][table]") {
    CHECK(group("f{}") == "f({})");
    CHECK(group("f{1, x = 2}") == "f({1, x=2})");
    CHECK(group("obj:m{1}") == "obj:m({1})");
    CHECK(group("f{1}{2}") == "f({1})({2})");
    CHECK(group("f{}'s'()") == "f({})('s')()");
}

TEST_CASE("G9.3 errors in table constructors", "[parser][table]") {
    CHECK(parse_error("return {1 2}") == "input:1: '}' expected near '2'");
    CHECK(parse_error("return {[1] 2}") == "input:1: '=' expected near '2'");
    CHECK(parse_error("return {[1 = 2}") == "input:1: ']' expected near '='");
    CHECK(parse_error("return {x = }") == "input:1: unexpected symbol near '}'");
    CHECK(parse_error("return {,}") == "input:1: unexpected symbol near ','");
    CHECK(parse_error("return {1,,}") == "input:1: unexpected symbol near ','");
    CHECK(parse_error("return {1;;}") == "input:1: unexpected symbol near ';'");
    CHECK(parse_error("return {end = 1}") == "input:1: unexpected symbol near 'end'");
    CHECK(parse_error("return {1, 2") == "input:1: '}' expected near <eof>");
    CHECK(parse_error("return {1,\n2") ==
          "input:2: '}' expected (to close '{' at line 1) near <eof>");
}

TEST_CASE("G6.5 the key of a named field must be a plain name", "[parser][table]") {
    // {x.y = 1} parses x.y as a positional value, then finds '=' where ','
    // or '}' should be. Same message as Lua.
    CHECK(parse_error("return {x.y = 1}") == "input:1: '}' expected near '='");
}

TEST_CASE("G6.3 the tree of a table", "[parser][table]") {
    CHECK(ast_of("return {1, x = 2, [k] = 3}") == "Chunk\n"
                                                  "  Return\n"
                                                  "    Table\n"
                                                  "      Item\n"
                                                  "        Integer 1\n"
                                                  "      Field x\n"
                                                  "        Integer 2\n"
                                                  "      Field\n"
                                                  "        Name k\n"
                                                  "        Integer 3\n");
}

// ---- Part 5: statements: calls, assignment and local ----

TEST_CASE("G3.1 a call as a statement", "[parser][statement]") {
    CHECK(ast_of("print('hi')") == "Chunk\n"
                                   "  CallStatement\n"
                                   "    Call\n"
                                   "      Name print\n"
                                   "      String \"hi\"\n");
    CHECK(ast_of("obj:m()") == "Chunk\n"
                               "  CallStatement\n"
                               "    MethodCall m\n"
                               "      Name obj\n");
    CHECK(ast_of("f{}") == "Chunk\n  CallStatement\n    Call\n      Name f\n      Table\n");
    CHECK(ast_of("a.b.c()") == "Chunk\n"
                               "  CallStatement\n"
                               "    Call\n"
                               "      Index\n"
                               "        Index\n"
                               "          Name a\n"
                               "          String \"b\"\n"
                               "        String \"c\"\n");
}

TEST_CASE("G3.6 assignment", "[parser][statement]") {
    CHECK(ast_of("x = 1") == "Chunk\n"
                             "  Assign\n"
                             "    Targets\n"
                             "      Name x\n"
                             "    Values\n"
                             "      Integer 1\n");
    CHECK(ast_of("a, b.c, d[1] = 1, 2") == "Chunk\n"
                                           "  Assign\n"
                                           "    Targets\n"
                                           "      Name a\n"
                                           "      Index\n"
                                           "        Name b\n"
                                           "        String \"c\"\n"
                                           "      Index\n"
                                           "        Name d\n"
                                           "        Integer 1\n"
                                           "    Values\n"
                                           "      Integer 1\n"
                                           "      Integer 2\n");
    CHECK(ast_of("f().x = 1") == "Chunk\n"
                                 "  Assign\n"
                                 "    Targets\n"
                                 "      Index\n"
                                 "        Call\n"
                                 "          Name f\n"
                                 "        String \"x\"\n"
                                 "    Values\n"
                                 "      Integer 1\n");
}

TEST_CASE("G3.2 local declarations", "[parser][statement]") {
    CHECK(ast_of("local x") == "Chunk\n  Local x\n");
    CHECK(ast_of("local x = 1") == "Chunk\n  Local x\n    Integer 1\n");
    CHECK(ast_of("local a, b, c = 1, 2") ==
          "Chunk\n  Local a, b, c\n    Integer 1\n    Integer 2\n");
}

TEST_CASE("G3.3 attributes", "[parser][statement]") {
    CHECK(ast_of("local x <const> = 5") == "Chunk\n  Local x <const>\n    Integer 5\n");
    CHECK(ast_of("local f <close> = nil") == "Chunk\n  Local f <close>\n    Nil\n");
    CHECK(ast_of("local a <const>, b, c <close>") == "Chunk\n  Local a <const>, b, c <close>\n");
    CHECK(ast_of("local x<const> =1") == "Chunk\n  Local x <const>\n    Integer 1\n");
    // Without the space, ">=" is one token (maximal munch), so real Lua fails too.
    CHECK(parse_error("local x<const>=1") == "input:1: '>' expected near '>='");
}

TEST_CASE("G2.6 statements need no separator", "[parser][statement]") {
    const std::string expected = "Chunk\n"
                                 "  Local a\n"
                                 "    Integer 1\n"
                                 "  Local b\n"
                                 "    Integer 2\n";
    CHECK(ast_of("local a = 1 local b = 2") == expected);
    CHECK(ast_of("local a = 1; local b = 2") == expected);
    CHECK(ast_of("local a = 1\nlocal b = 2") == expected);
    CHECK(ast_of(";;local a = 1;;;local b = 2;") == expected); // ';' leaves nothing behind
}

TEST_CASE("G2.2 statements followed by a return", "[parser][statement]") {
    CHECK(ast_of("local x = 1 return x") == "Chunk\n"
                                            "  Local x\n"
                                            "    Integer 1\n"
                                            "  Return\n"
                                            "    Name x (local 0)\n");
    CHECK(parse_error("return 1 x = 2") == "input:1: <eof> expected near 'x'");
}

TEST_CASE("G3.8 an expression statement must be a call or an assignment", "[parser][statement]") {
    CHECK(parse_error("x") == "input:1: syntax error near <eof>");
    CHECK(parse_error("x y") == "input:1: syntax error near 'y'");
    CHECK(parse_error("a.b") == "input:1: syntax error near <eof>");
    CHECK(parse_error("(f)") == "input:1: syntax error near <eof>");
    CHECK(parse_error("(f())") == "input:1: syntax error near <eof>"); // parentheses are not a call
    CHECK(parse_error("x\n\ny") == "input:3: syntax error near 'y'");
}

TEST_CASE("G3.8 a statement cannot start with a literal or an operator", "[parser][statement]") {
    CHECK(parse_error("1 + 2") == "input:1: unexpected symbol near '1'");
    CHECK(parse_error("'s':upper()") == "input:1: unexpected symbol near ''s''");
    CHECK(parse_error("- x") == "input:1: unexpected symbol near '-'");
}

TEST_CASE("G3.9 assignment targets must be variables", "[parser][statement]") {
    CHECK(parse_error("f() = 1") == "input:1: syntax error near '='");
    CHECK(parse_error("(a) = 1") == "input:1: syntax error near '='");
    CHECK(parse_error("a, f() = 1") == "input:1: syntax error near '='");
    CHECK(parse_error("f(), a = 1") == "input:1: syntax error near ','");
}

TEST_CASE("G3.6 broken assignments", "[parser][statement]") {
    CHECK(parse_error("x =") == "input:1: unexpected symbol near <eof>");
    CHECK(parse_error("x = y = 1") == "input:1: unexpected symbol near '='"); // no chained =
    CHECK(parse_error("a, = 1") == "input:1: unexpected symbol near '='");
    CHECK(parse_error("a, b") == "input:1: '=' expected near <eof>");
}

TEST_CASE("G3.2 broken local declarations", "[parser][statement]") {
    CHECK(parse_error("local") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("local 1") == "input:1: <name> expected near '1'");
    CHECK(parse_error("local x,") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("local x, = 1") == "input:1: <name> expected near '='");
    CHECK(parse_error("local x =") == "input:1: unexpected symbol near <eof>");
}

TEST_CASE("G3.15 attribute errors", "[parser][statement]") {
    CHECK(parse_error("local x <const") == "input:1: '>' expected near <eof>");
    CHECK(parse_error("local x <>") == "input:1: <name> expected near '>'");
    // Semantic errors have no "near" part (G9.5).
    CHECK(parse_error("local x <foo> = 1") == "input:1: unknown attribute 'foo'");
    CHECK(parse_error("local x <CONST>") == "input:1: unknown attribute 'CONST'");
    CHECK(parse_error("local a <close>, b <close>") ==
          "input:1: multiple to-be-closed variables in local list");
}

TEST_CASE("G3.1 statement nodes record their line", "[parser][statement]") {
    Parser parser{"\nlocal x\n\nf()\nx = 1"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto statements = ast->statements(ast->root());
    REQUIRE(statements.size() == 3);
    CHECK(ast->line(statements[0]) == 2);
    CHECK(ast->line(statements[1]) == 4);
    CHECK(ast->line(statements[2]) == 5);
}

// ---- Part 6: blocks and control flow ----

TEST_CASE("G3.1 do blocks", "[parser][control]") {
    CHECK(ast_of("do end") == "Chunk\n  Do\n");
    CHECK(ast_of("do local x = 1 f(x) end") == "Chunk\n"
                                               "  Do\n"
                                               "    Local x\n"
                                               "      Integer 1\n"
                                               "    CallStatement\n"
                                               "      Call\n"
                                               "        Name f\n"
                                               "        Name x (local 0)\n");
    CHECK(ast_of("do do end end") == "Chunk\n  Do\n    Do\n");
    CHECK(ast_of("do return 1 end") == "Chunk\n  Do\n    Return\n      Integer 1\n");
}

TEST_CASE("G3.1 while loops", "[parser][control]") {
    CHECK(ast_of("while x do x = x - 1 end") == "Chunk\n"
                                                "  While\n"
                                                "    Name x\n"
                                                "    Body\n"
                                                "      Assign\n"
                                                "        Targets\n"
                                                "          Name x\n"
                                                "        Values\n"
                                                "          Binary -\n"
                                                "            Name x\n"
                                                "            Integer 1\n");
    CHECK(ast_of("while true do end") == "Chunk\n  While\n    True\n    Body\n");
}

TEST_CASE("G3.1 repeat loops", "[parser][control]") {
    CHECK(ast_of("repeat f() until x") == "Chunk\n"
                                          "  Repeat\n"
                                          "    Body\n"
                                          "      CallStatement\n"
                                          "        Call\n"
                                          "          Name f\n"
                                          "    Until\n"
                                          "      Name x\n");
    CHECK(ast_of("repeat until 1 > 2") == "Chunk\n"
                                          "  Repeat\n"
                                          "    Body\n"
                                          "    Until\n"
                                          "      Binary >\n"
                                          "        Integer 1\n"
                                          "        Integer 2\n");
}

TEST_CASE("G3.1 if statements", "[parser][control]") {
    CHECK(ast_of("if a then f() end") == "Chunk\n"
                                         "  If\n"
                                         "    If\n"
                                         "      Name a\n"
                                         "      Then\n"
                                         "        CallStatement\n"
                                         "          Call\n"
                                         "            Name f\n");
    CHECK(ast_of("if a then elseif b then elseif c then else end") == "Chunk\n"
                                                                      "  If\n"
                                                                      "    If\n"
                                                                      "      Name a\n"
                                                                      "      Then\n"
                                                                      "    ElseIf\n"
                                                                      "      Name b\n"
                                                                      "      Then\n"
                                                                      "    ElseIf\n"
                                                                      "      Name c\n"
                                                                      "      Then\n"
                                                                      "    Else\n");
}

TEST_CASE("G3.1 an empty else is kept, a missing one is not", "[parser][control]") {
    cpplua::Ast ast;
    CHECK(std::get<cpplua::IfStat>(only_statement("if a then else end", ast)).has_else);
    CHECK_FALSE(std::get<cpplua::IfStat>(only_statement("if a then end", ast)).has_else);
}

TEST_CASE("G3.10 numeric for", "[parser][control]") {
    CHECK(ast_of("for i = 1, 10 do end") == "Chunk\n"
                                            "  NumericFor i\n"
                                            "    Integer 1\n"
                                            "    Integer 10\n"
                                            "    Body\n");
    CHECK(ast_of("for i = 10, 1, -1 do f(i) end") == "Chunk\n"
                                                     "  NumericFor i\n"
                                                     "    Integer 10\n"
                                                     "    Integer 1\n"
                                                     "    Unary -\n"
                                                     "      Integer 1\n"
                                                     "    Body\n"
                                                     "      CallStatement\n"
                                                     "        Call\n"
                                                     "          Name f\n"
                                                     "          Name i (local 3)\n");
    cpplua::Ast ast;
    CHECK_FALSE(
        std::get<cpplua::NumericForStat>(only_statement("for i = 1, 2 do end", ast)).has_step);
    CHECK(std::get<cpplua::NumericForStat>(only_statement("for i = 1, 2, 3 do end", ast)).has_step);
}

TEST_CASE("G3.1 generic for", "[parser][control]") {
    CHECK(ast_of("for k, v in pairs(t) do end") == "Chunk\n"
                                                   "  GenericFor k, v\n"
                                                   "    In\n"
                                                   "      Call\n"
                                                   "        Name pairs\n"
                                                   "        Name t\n"
                                                   "    Body\n");
    CHECK(ast_of("for x in f, s, c do end") == "Chunk\n"
                                               "  GenericFor x\n"
                                               "    In\n"
                                               "      Name f\n"
                                               "      Name s\n"
                                               "      Name c\n"
                                               "    Body\n");
}

TEST_CASE("G3.1 control flow nests", "[parser][control]") {
    CHECK(ast_of("while a do if b then for i = 1, 2 do repeat until c end end end") ==
          "Chunk\n"
          "  While\n"
          "    Name a\n"
          "    Body\n"
          "      If\n"
          "        If\n"
          "          Name b\n"
          "          Then\n"
          "            NumericFor i\n"
          "              Integer 1\n"
          "              Integer 2\n"
          "              Body\n"
          "                Repeat\n"
          "                  Body\n"
          "                  Until\n"
          "                    Name c\n");
}

TEST_CASE("G2.5 return is last in its own block", "[parser][control]") {
    CHECK(ast_of("if x then return 1 else return 2 end") == "Chunk\n"
                                                            "  If\n"
                                                            "    If\n"
                                                            "      Name x\n"
                                                            "      Then\n"
                                                            "        Return\n"
                                                            "          Integer 1\n"
                                                            "    Else\n"
                                                            "      Return\n"
                                                            "        Integer 2\n");
    CHECK(parse_error("do return 1 x = 2 end") == "input:1: 'end' expected near 'x'");
    // 'until' ends a block too, so a return may come right before it.
    CHECK(ast_of("repeat return until x") == "Chunk\n"
                                             "  Repeat\n"
                                             "    Body\n"
                                             "      Return\n"
                                             "    Until\n"
                                             "      Name x\n");
}

TEST_CASE("G9.3 missing closing words", "[parser][control]") {
    // On the same line: the short form.
    CHECK(parse_error("do") == "input:1: 'end' expected near <eof>");
    CHECK(parse_error("while x do") == "input:1: 'end' expected near <eof>");
    CHECK(parse_error("repeat") == "input:1: 'until' expected near <eof>");
    CHECK(parse_error("if x then") == "input:1: 'end' expected near <eof>");
    CHECK(parse_error("for i = 1, 2 do") == "input:1: 'end' expected near <eof>");
    // On a later line: the message names the opening word and its line.
    CHECK(parse_error("do\n") == "input:2: 'end' expected (to close 'do' at line 1) near <eof>");
    CHECK(parse_error("x = 1\nwhile x do\n\n") ==
          "input:4: 'end' expected (to close 'while' at line 2) near <eof>");
    CHECK(parse_error("repeat\nf()") ==
          "input:2: 'until' expected (to close 'repeat' at line 1) near <eof>");
    CHECK(parse_error("if x then\nelse\n") ==
          "input:3: 'end' expected (to close 'if' at line 1) near <eof>");
    CHECK(parse_error("for k in t do\nf()") ==
          "input:2: 'end' expected (to close 'for' at line 1) near <eof>");
}

TEST_CASE("G9.3 missing middle words", "[parser][control]") {
    CHECK(parse_error("while x f() end") == "input:1: 'do' expected near 'f'");
    CHECK(parse_error("if x f() end") == "input:1: 'then' expected near 'f'");
    CHECK(parse_error("if x then elseif y end") == "input:1: 'then' expected near 'end'");
    CHECK(parse_error("for i = 1, 2 f() end") == "input:1: 'do' expected near 'f'");
    CHECK(parse_error("for k in t f() end") == "input:1: 'do' expected near 'f'");
    CHECK(parse_error("if x then else elseif y then end") ==
          "input:1: 'end' expected near 'elseif'");
}

TEST_CASE("G3.10 broken for loops", "[parser][control]") {
    CHECK(parse_error("for") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("for 1 = 2") == "input:1: <name> expected near '1'");
    CHECK(parse_error("for x") == "input:1: '=' or 'in' expected near <eof>");
    CHECK(parse_error("for x do end") == "input:1: '=' or 'in' expected near 'do'");
    CHECK(parse_error("for i = 1 do end") == "input:1: ',' expected near 'do'");
    CHECK(parse_error("for a, b = 1, 2 do end") == "input:1: 'in' expected near '='");
    CHECK(parse_error("for k, in t do end") == "input:1: <name> expected near 'in'");
    CHECK(parse_error("for k in do end") == "input:1: unexpected symbol near 'do'");
}

TEST_CASE("G2.2 block-ending words cannot start a chunk", "[parser][control]") {
    CHECK(parse_error("end") == "input:1: <eof> expected near 'end'");
    CHECK(parse_error("else") == "input:1: <eof> expected near 'else'");
    CHECK(parse_error("until x") == "input:1: <eof> expected near 'until'");
    CHECK(parse_error("do end end") == "input:1: <eof> expected near 'end'");
}

// ---- Part 7: functions ----

TEST_CASE("G5.1 function expressions", "[parser][function]") {
    CHECK(ast_of("return function() end") == "Chunk\n  Return\n    Function()\n");
    CHECK(ast_of("return function(a, b) return a + b end") == "Chunk\n"
                                                              "  Return\n"
                                                              "    Function(a, b)\n"
                                                              "      Return\n"
                                                              "        Binary +\n"
                                                              "          Name a (local 0)\n"
                                                              "          Name b (local 1)\n");
    CHECK(group("function() end") == "function");
    CHECK(group("f(function() end, 1)") == "f(function, 1)");
    CHECK(group("{function() end}") == "{function}");
}

TEST_CASE("G5.3 parameter lists", "[parser][function]") {
    CHECK(ast_of("return function(a) end") == "Chunk\n  Return\n    Function(a)\n");
    CHECK(ast_of("return function(...) end") == "Chunk\n  Return\n    Function(...)\n");
    CHECK(ast_of("return function(a, b, ...) end") == "Chunk\n  Return\n    Function(a, b, ...)\n");
}

TEST_CASE("G5.4 broken parameter lists", "[parser][function]") {
    CHECK(parse_error("return function(1) end") == "input:1: <name> or '...' expected near '1'");
    CHECK(parse_error("return function(a,) end") == "input:1: <name> or '...' expected near ')'");
    CHECK(parse_error("return function(..., a) end") == "input:1: ')' expected near ','");
    CHECK(parse_error("return function(a b) end") == "input:1: ')' expected near 'b'");
    CHECK(parse_error("return function end") == "input:1: '(' expected near 'end'");
}

TEST_CASE("G5.2 a function body needs its end", "[parser][function]") {
    CHECK(parse_error("return function()") == "input:1: 'end' expected near <eof>");
    CHECK(parse_error("f = function()\n  return 1\n") ==
          "input:3: 'end' expected (to close 'function' at line 1) near <eof>");
}

TEST_CASE("G4.7 a function expression cannot be called directly", "[parser][function]") {
    // Like a literal: it is a simple expression, not a prefix expression.
    CHECK(parse_error("return function() end ()") == "input:1: <eof> expected near '('");
    CHECK(group("(function() end)()") == "[function]()");
}

TEST_CASE("G3.11 function statements", "[parser][function]") {
    CHECK(ast_of("function f() end") == "Chunk\n"
                                        "  FunctionStatement\n"
                                        "    Name f\n"
                                        "    Function()\n");
    CHECK(ast_of("function a.b.c(x) end") == "Chunk\n"
                                             "  FunctionStatement\n"
                                             "    Index\n"
                                             "      Index\n"
                                             "        Name a\n"
                                             "        String \"b\"\n"
                                             "      String \"c\"\n"
                                             "    Function(x)\n");
}

TEST_CASE("G3.11 methods get self as their first parameter", "[parser][function]") {
    CHECK(ast_of("function obj:m(x) end") == "Chunk\n"
                                             "  FunctionStatement (method)\n"
                                             "    Index\n"
                                             "      Name obj\n"
                                             "      String \"m\"\n"
                                             "    Function(self, x)\n");
    CHECK(ast_of("function a.b:m(...) end") == "Chunk\n"
                                               "  FunctionStatement (method)\n"
                                               "    Index\n"
                                               "      Index\n"
                                               "        Name a\n"
                                               "        String \"b\"\n"
                                               "      String \"m\"\n"
                                               "    Function(self, ...)\n");
}

TEST_CASE("G3.12 local functions", "[parser][function]") {
    CHECK(ast_of("local function f(n) return f(n) end") == "Chunk\n"
                                                           "  LocalFunction f\n"
                                                           "    Function(n) [upvalues: f]\n"
                                                           "      Return\n"
                                                           "        Call\n"
                                                           "          Name f (upvalue 0)\n"
                                                           "          Name n (local 0)\n");
    // Not the same statement as a local holding a function expression.
    CHECK(ast_of("local f = function() end") == "Chunk\n  Local f\n    Function()\n");
}

TEST_CASE("G3.5 broken function names", "[parser][function]") {
    CHECK(parse_error("function") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("function f") == "input:1: '(' expected near <eof>");
    CHECK(parse_error("function a.() end") == "input:1: <name> expected near '('");
    CHECK(parse_error("function a:b.c() end") == "input:1: '(' expected near '.'");
    CHECK(parse_error("function a[1]() end") == "input:1: '(' expected near '['");
    CHECK(parse_error("function a.1() end") == "input:1: '(' expected near '.1'"); // .1 is a number
    CHECK(parse_error("local function") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("local function a.b() end") == "input:1: '(' expected near '.'");
}

TEST_CASE("G4.12 G2.4 '...' only inside a vararg function", "[parser][function]") {
    // The main chunk is vararg.
    CHECK(ast_of("return ...") == "Chunk\n  Return\n    Vararg\n");
    CHECK(ast_of("return function(...) return ... end") == "Chunk\n"
                                                           "  Return\n"
                                                           "    Function(...)\n"
                                                           "      Return\n"
                                                           "        Vararg\n");
    CHECK(parse_error("return function() return ... end") ==
          "input:1: cannot use '...' outside a vararg function near '...'");
    CHECK(parse_error("function f(a) local t = {...} end") ==
          "input:1: cannot use '...' outside a vararg function near '...'");
    // A non-vararg function inside a vararg one cannot see the outer '...'.
    CHECK(parse_error("return function(...) return function() return ... end end") ==
          "input:1: cannot use '...' outside a vararg function near '...'");
    // After a nested function ends, '...' works again in the outer one.
    CHECK(ast_of("return function(...) local f = function() end return ... end") ==
          "Chunk\n"
          "  Return\n"
          "    Function(...)\n"
          "      Local f\n"
          "        Function()\n"
          "      Return\n"
          "        Vararg\n");
}

TEST_CASE("G5.2 a function records where it starts and ends", "[parser][function]") {
    Parser parser{"\nlocal f = function(a)\n  return a\nend"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto statement = ast->statements(ast->root())[0];
    const auto& local = std::get<cpplua::LocalStat>(ast->statement(statement));
    const auto value = ast->expressions(local.values)[0];
    const auto& function =
        ast->function(std::get<cpplua::FunctionExpr>(ast->expression(value)).function);

    CHECK(function.line == 2);
    CHECK(function.end_line == 4);
    CHECK_FALSE(function.is_vararg);
    CHECK(ast->local_names(function.parameters).size() == 1);
}

// ---- Part 8: goto, labels and break ----

TEST_CASE("G3.4 labels", "[parser][jump]") {
    CHECK(ast_of("::top::") == "Chunk\n  Label top\n");
    CHECK(ast_of(":: spaced ::") == "Chunk\n  Label spaced\n");
}

TEST_CASE("G8.2 goto", "[parser][jump]") {
    CHECK(ast_of("goto done ::done::") == "Chunk\n  Goto done\n  Label done\n");
    CHECK(ast_of("::top:: goto top") == "Chunk\n  Label top\n  Goto top\n");
}

TEST_CASE("G8.6 break", "[parser][jump]") {
    CHECK(ast_of("while true do break end") == "Chunk\n"
                                               "  While\n"
                                               "    True\n"
                                               "    Body\n"
                                               "      Break\n");
}

TEST_CASE("G8.7 break and goto may appear anywhere in a block", "[parser][jump]") {
    // Unlike return, they do not have to be last.
    CHECK(ast_of("while x do break f() end") == "Chunk\n"
                                                "  While\n"
                                                "    Name x\n"
                                                "    Body\n"
                                                "      Break\n"
                                                "      CallStatement\n"
                                                "        Call\n"
                                                "          Name f\n");
    CHECK(ast_of("goto a goto b ::a:: ::b::") == "Chunk\n"
                                                 "  Goto a\n"
                                                 "  Goto b\n"
                                                 "  Label a\n"
                                                 "  Label b\n");
}

TEST_CASE("G8.5 the continue idiom", "[parser][jump]") {
    CHECK(ast_of("for i = 1, 3 do\n"
                 "  if i == 2 then goto continue end\n"
                 "  local x = i\n"
                 "  ::continue::\n"
                 "end") == "Chunk\n"
                           "  NumericFor i\n"
                           "    Integer 1\n"
                           "    Integer 3\n"
                           "    Body\n"
                           "      If\n"
                           "        If\n"
                           "          Binary ==\n"
                           "            Name i (local 3)\n"
                           "            Integer 2\n"
                           "          Then\n"
                           "            Goto continue\n"
                           "      Local x\n"
                           "        Name i (local 3)\n"
                           "      Label continue\n");
}

TEST_CASE("G9.3 broken goto and labels", "[parser][jump]") {
    CHECK(parse_error("goto") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("goto 1") == "input:1: <name> expected near '1'");
    CHECK(parse_error("goto end") == "input:1: <name> expected near 'end'");
    CHECK(parse_error("::") == "input:1: <name> expected near <eof>");
    CHECK(parse_error("::top") == "input:1: '::' expected near <eof>");
    CHECK(parse_error("::top: x") == "input:1: '::' expected near ':'");
    CHECK(parse_error("::break::") == "input:1: <name> expected near 'break'");
}

TEST_CASE("G4.1 goto is a keyword, not a name", "[parser][jump]") {
    CHECK(parse_error("local goto = 1") == "input:1: <name> expected near 'goto'");
}

// ---- Part 9: scopes and compile-time checks ----
//
// These are semantic errors: no "near" part (G9.5).

TEST_CASE("G8.6 break must be inside a loop", "[parser][scope]") {
    CHECK(parse_error("break") == "input:1: break outside loop at line 1");
    CHECK(parse_error("if x then break end") == "input:1: break outside loop at line 1");
    // Reported at the end of the function, so the prefix is the last line.
    CHECK(parse_error("x = 1\nbreak\n\n") == "input:4: break outside loop at line 2");
    // A function is a new scope: the loop outside it does not count.
    CHECK(parse_error("while true do local f = function() break end end") ==
          "input:1: break outside loop at line 1");
}

TEST_CASE("G8.6 break works in every loop and nested blocks", "[parser][scope]") {
    CHECK_NOTHROW(ast_of("while true do break end"));
    CHECK_NOTHROW(ast_of("repeat break until true"));
    CHECK_NOTHROW(ast_of("for i = 1, 2 do break end"));
    CHECK_NOTHROW(ast_of("for k in pairs(t) do break end"));
    CHECK_NOTHROW(ast_of("while true do if x then do break end end end"));
    CHECK_NOTHROW(ast_of("local f = function() while true do break end end"));
}

TEST_CASE("G8.2 a goto needs a visible label", "[parser][scope]") {
    CHECK(parse_error("goto nowhere") ==
          "input:1: no visible label 'nowhere' for <goto> at line 1");
    // A label inside a nested block is not visible from outside.
    CHECK(parse_error("goto l do ::l:: end") ==
          "input:1: no visible label 'l' for <goto> at line 1");
    // Nor is a label of an enclosing function.
    CHECK(parse_error("::a:: local f = function() goto a end") ==
          "input:1: no visible label 'a' for <goto> at line 1");
}

TEST_CASE("G8.1 visible labels", "[parser][scope]") {
    CHECK_NOTHROW(ast_of("goto done ::done::"));                   // forward
    CHECK_NOTHROW(ast_of("::top:: local x = 1 goto top"));         // backward, out of a scope
    CHECK_NOTHROW(ast_of("do goto out end ::out::"));              // out of a nested block
    CHECK_NOTHROW(ast_of("::a:: do goto a end"));                  // to an enclosing block
    CHECK_NOTHROW(ast_of("do ::a:: end do ::a:: end"));            // separate blocks
    CHECK_NOTHROW(ast_of("::a:: local f = function() ::a:: end")); // separate functions
}

TEST_CASE("G8.3 a label cannot be defined twice where both are visible", "[parser][scope]") {
    CHECK(parse_error("::a:: ::a::") == "input:1: label 'a' already defined on line 1");
    CHECK(parse_error("::a:: do ::a:: end") == "input:1: label 'a' already defined on line 1");
    // Like Lua, consecutive labels are registered last first, so the
    // message names the later one.
    CHECK(parse_error("::a::\n::a::") == "input:2: label 'a' already defined on line 2");
}

TEST_CASE("G8.4 a goto cannot jump into the scope of a local", "[parser][scope]") {
    CHECK(parse_error("goto l local x = 1 ::l:: print(x)") ==
          "input:1: <goto l> at line 1 jumps into the scope of local 'x'");
    CHECK(parse_error("goto l\nlocal a, b = 1, 2\n::l::\nprint(a)") ==
          "input:4: <goto l> at line 1 jumps into the scope of local 'a'");
}

TEST_CASE("G8.5 a label at the end of its block is outside the block's locals", "[parser][scope]") {
    CHECK_NOTHROW(ast_of("goto l local x = 1 ::l::"));
    CHECK_NOTHROW(ast_of("goto l local x = 1 ::l:: ;; ::m::"));
    CHECK_NOTHROW(ast_of("for i = 1, 3 do\n"
                         "  if i == 2 then goto continue end\n"
                         "  local x = i\n"
                         "  ::continue::\n"
                         "end"));
    // Before 'until' a label is not at the end: the condition still sees x.
    CHECK(parse_error("repeat goto l local x ::l:: until x") ==
          "input:1: <goto l> at line 1 jumps into the scope of local 'x'");
    // Nor before a return.
    CHECK(parse_error("goto l local x ::l:: return") ==
          "input:1: <goto l> at line 1 jumps into the scope of local 'x'");
}

TEST_CASE("G3.15 const and close variables cannot be assigned", "[parser][scope]") {
    CHECK(parse_error("local x <const> = 1 x = 2") ==
          "input:1: attempt to assign to const variable 'x'");
    CHECK(parse_error("local x <close> = nil x = 1") ==
          "input:1: attempt to assign to const variable 'x'");
    CHECK(parse_error("local x <const>\nx = 1") ==
          "input:2: attempt to assign to const variable 'x'");
    CHECK(parse_error("local a <const> = 1 b, a = 1, 2") ==
          "input:1: attempt to assign to const variable 'a'");
    // From a nested function: an upvalue keeps its attribute.
    CHECK(parse_error("local x <const> = 1 local function f() x = 2 end") ==
          "input:1: attempt to assign to const variable 'x'");
    // A function statement assigns too.
    CHECK(parse_error("local x <const> = 1 function x() end") ==
          "input:1: attempt to assign to const variable 'x'");
}

TEST_CASE("G3.15 what a const variable still allows", "[parser][scope]") {
    CHECK_NOTHROW(ast_of("local t <const> = {} t.x = 1 t[1] = 2")); // fields
    CHECK_NOTHROW(ast_of("local t <const> = {} function t.f() end"));
    CHECK_NOTHROW(ast_of("local x <const> = 1 do local x = 2 x = 3 end")); // shadowed
    CHECK_NOTHROW(ast_of("x = 1 local x <const> = 2"));                    // a global before
    CHECK_NOTHROW(ast_of("for i = 1, 2 do i = 3 end"));                    // loop variables
}

TEST_CASE("G3.13 a new local is visible only after its statement", "[parser][scope]") {
    // Inside its own initializer, f is still the global f, so this assigns
    // the global, not the const local.
    CHECK_NOTHROW(ast_of("local f <const> = function() f = 1 end"));
    CHECK(parse_error("local x = 1 local x <const> = x x = 2") ==
          "input:1: attempt to assign to const variable 'x'");
}

TEST_CASE("G3.14 the until condition sees the body's locals", "[parser][scope]") {
    CHECK(parse_error("repeat local x <const> = 1 until (function() x = 2 end)") ==
          "input:1: attempt to assign to const variable 'x'");
}

TEST_CASE("G10 at most 200 local variables per function", "[parser][scope]") {
    const auto locals = [](int count) {
        std::string source = "local v1";
        for (int i = 2; i <= count; ++i) {
            source += ", v" + std::to_string(i);
        }
        return source;
    };
    CHECK_NOTHROW(ast_of(locals(200)));
    CHECK(parse_error(locals(201)) ==
          "input:1: too many local variables (limit is 200) in main function near <eof>");
    CHECK(parse_error("local f = function()\n" + locals(201) + "\nend") ==
          "input:3: too many local variables (limit is 200) in function at line 1 near 'end'");
    // Locals of finished blocks no longer count.
    CHECK_NOTHROW(ast_of("do " + locals(150) + " end do " + locals(150) + " end"));
}

TEST_CASE("G10 nesting is limited instead of overflowing the stack", "[parser][scope]") {
    const auto repeat = [](std::string_view text, int count) {
        std::string out;
        for (int i = 0; i < count; ++i) {
            out += text;
        }
        return out;
    };
    // Shallow nesting is fine.
    CHECK_NOTHROW(ast_of("return " + repeat("(", 100) + "1" + repeat(")", 100)));
    // Deep nesting is an error, not a crash.
    CHECK(parse_error("return " + repeat("(", 1000) + "1" + repeat(")", 1000)) ==
          "input:1: C stack overflow");
    CHECK(parse_error(repeat("do ", 1000) + repeat("end ", 1000)) == "input:1: C stack overflow");
    CHECK(parse_error("return 1" + repeat(" .. 1", 1000)) == "input:1: C stack overflow");
    // Long left-associative chains are a loop, not recursion, so they are fine.
    CHECK_NOTHROW(ast_of("return 1" + repeat(" + 1", 10000)));
}

// ---- Part 10: name resolution ----

namespace {

// Parses `source` and returns the resolution of the first name in its
// return statement's first value (which must be a NameExpr).
cpplua::NameExpr returned_name(std::string_view source, cpplua::Ast& ast) {
    Parser parser{source, "input"};
    auto parsed = parser.parse_chunk();
    if (!parsed) {
        FAIL("unexpected parse error: " << parsed.error().message);
    }
    ast = std::move(*parsed);
    const auto statements = ast.statements(ast.root());
    const auto& ret = std::get<cpplua::ReturnStat>(ast.statement(statements.back()));
    return std::get<cpplua::NameExpr>(ast.expression(ast.expressions(ret.values)[0]));
}

using Kind = cpplua::VariableRef::Kind;

cpplua::VariableRef local(std::uint32_t slot) {
    return {.kind = Kind::Local, .index = slot};
}

cpplua::VariableRef upvalue(std::uint32_t index) {
    return {.kind = Kind::Upvalue, .index = index};
}

} // namespace

TEST_CASE("G5.6 locals get their slot", "[parser][resolve]") {
    cpplua::Ast ast;
    CHECK(returned_name("local a return a", ast).variable == local(0));
    CHECK(returned_name("local a, b return b", ast).variable == local(1));
    // An inner declaration shadows an outer one, and takes the next slot.
    CHECK(returned_name("local x local x return x", ast).variable == local(1));
    // Slots of a finished block are reused.
    CHECK(returned_name("do local a, b end local c return c", ast).variable == local(0));
}

TEST_CASE("G5.6 for loops reserve hidden slots first", "[parser][resolve]") {
    CHECK(ast_of("for i = 1, 2 do return i end") == "Chunk\n"
                                                    "  NumericFor i\n"
                                                    "    Integer 1\n"
                                                    "    Integer 2\n"
                                                    "    Body\n"
                                                    "      Return\n"
                                                    "        Name i (local 3)\n");
    CHECK(ast_of("for k, v in t do return v end") == "Chunk\n"
                                                     "  GenericFor k, v\n"
                                                     "    In\n"
                                                     "      Name t\n"
                                                     "    Body\n"
                                                     "      Return\n"
                                                     "        Name v (local 5)\n");
}

TEST_CASE("G5.6 globals go through _ENV", "[parser][resolve]") {
    cpplua::Ast ast;
    // In the main chunk, _ENV is upvalue 0.
    const auto global = returned_name("return x", ast);
    CHECK(global.variable.kind == Kind::Global);
    CHECK(global.environment == upvalue(0));

    // `local _ENV` changes where globals come from.
    const auto custom = returned_name("local _ENV = {} return x", ast);
    CHECK(custom.variable.kind == Kind::Global);
    CHECK(custom.environment == local(0));
}

TEST_CASE("G5.6 a nested function captures what it uses as upvalues", "[parser][resolve]") {
    CHECK(ast_of("local x return function() return x end") == "Chunk\n"
                                                              "  Local x\n"
                                                              "  Return\n"
                                                              "    Function() [upvalues: x]\n"
                                                              "      Return\n"
                                                              "        Name x (upvalue 0)\n");
    // Using it twice still makes one upvalue.
    CHECK(ast_of("local x return function() return x, x end") == "Chunk\n"
                                                                 "  Local x\n"
                                                                 "  Return\n"
                                                                 "    Function() [upvalues: x]\n"
                                                                 "      Return\n"
                                                                 "        Name x (upvalue 0)\n"
                                                                 "        Name x (upvalue 0)\n");
    // A global in a nested function needs _ENV, which comes in as an upvalue.
    CHECK(ast_of("return function() return print end") == "Chunk\n"
                                                          "  Return\n"
                                                          "    Function() [upvalues: _ENV]\n"
                                                          "      Return\n"
                                                          "        Name print\n");
}

TEST_CASE("G5.6 upvalues chain through every function in between", "[parser][resolve]") {
    Parser parser{"local x return function() return function() return x end end"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto& ret = std::get<cpplua::ReturnStat>(ast->statement(ast->statements(ast->root())[1]));
    const auto& middle = ast->function(
        std::get<cpplua::FunctionExpr>(ast->expression(ast->expressions(ret.values)[0])).function);
    const auto& middle_ret =
        std::get<cpplua::ReturnStat>(ast->statement(ast->statements(middle.body)[0]));
    const auto& inner = ast->function(
        std::get<cpplua::FunctionExpr>(ast->expression(ast->expressions(middle_ret.values)[0]))
            .function);

    // The middle function captures the main chunk's local 0...
    REQUIRE(ast->upvalues(middle.upvalues).size() == 1);
    CHECK(ast->upvalues(middle.upvalues)[0].from_parent_local);
    CHECK(ast->upvalues(middle.upvalues)[0].index == 0);
    // ...and the inner one captures the middle one's upvalue 0.
    REQUIRE(ast->upvalues(inner.upvalues).size() == 1);
    CHECK_FALSE(ast->upvalues(inner.upvalues)[0].from_parent_local);
    CHECK(ast->upvalues(inner.upvalues)[0].index == 0);
}

TEST_CASE("G2.4 the main chunk is a vararg function with _ENV", "[parser][resolve]") {
    Parser parser{"return 1"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const auto& main = ast->function(ast->main_function());
    CHECK(main.is_vararg);
    CHECK(main.parameters.count == 0);
    REQUIRE(ast->upvalues(main.upvalues).size() == 1);
    CHECK(ast->text(ast->upvalues(main.upvalues)[0].name) == "_ENV");
}

TEST_CASE("G3.12 local function sees itself, local f = function does not", "[parser][resolve]") {
    CHECK(ast_of("local function f() return f end") == "Chunk\n"
                                                       "  LocalFunction f\n"
                                                       "    Function() [upvalues: f]\n"
                                                       "      Return\n"
                                                       "        Name f (upvalue 0)\n");
    CHECK(ast_of("local f = function() return f end") == "Chunk\n"
                                                         "  Local f\n"
                                                         "    Function() [upvalues: _ENV]\n"
                                                         "      Return\n"
                                                         "        Name f\n");
}

TEST_CASE("G10 the parser leaves the limit of 255 upvalues to the compiler", "[parser][resolve]") {
    // 199 locals in the main chunk (f is the 200th) and more in f: g returns
    // all of them, so it needs one upvalue for each.
    const auto names = [](std::string_view prefix, int count) {
        std::string out;
        for (int i = 1; i <= count; ++i) {
            out += (i == 1 ? "" : ", ") + std::string(prefix) + std::to_string(i);
        }
        return out;
    };
    const auto source = [&](int f_locals) {
        return "local " + names("a", 199) + "\n" +      // line 1
               "local function f()\n" +                 // line 2
               "local " + names("b", f_locals) + "\n" + // line 3
               "local function g()\n" +                 // line 4
               "return " + names("a", 199) + ", " + names("b", f_locals) + "\n" + "end\nend\n";
    };
    CHECK_NOTHROW(ast_of(source(56))); // exactly 255
    // The compiler checks the limit, not the parser: it knows which <const>
    // locals are compile-time constants, which need no upvalue (issue #11).
    CHECK_NOTHROW(ast_of(source(57)));
}

// ---- Part 11: the AST as a graph ----

TEST_CASE("the DOT graph links each node to its parent", "[parser][ast]") {
    Parser parser{"do local x end y = 1"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    // print_ast:            ids:
    //   Chunk                n0
    //     Do                 n1
    //       Local x          n2
    //     Assign             n3
    //       Targets          n4
    //         Name y         n5
    //       Values           n6
    //         Integer 1      n7
    CHECK(cpplua::print_ast_dot(*ast) == "digraph ast {\n"
                                         "  ordering=out;\n"
                                         "  node [shape=box, fontname=\"monospace\"];\n"
                                         "  n0 [label=\"Chunk\"];\n"
                                         "  n1 [label=\"Do\"];\n"
                                         "  n0 -> n1;\n"
                                         "  n2 [label=\"Local x\"];\n"
                                         "  n1 -> n2;\n"
                                         "  n3 [label=\"Assign\"];\n"
                                         "  n0 -> n3;\n"
                                         "  n4 [label=\"Targets\"];\n"
                                         "  n3 -> n4;\n"
                                         "  n5 [label=\"Name y\"];\n"
                                         "  n4 -> n5;\n"
                                         "  n6 [label=\"Values\"];\n"
                                         "  n3 -> n6;\n"
                                         "  n7 [label=\"Integer 1\"];\n"
                                         "  n6 -> n7;\n"
                                         "}\n");
}

TEST_CASE("the DOT graph has one node per printed line", "[parser][ast]") {
    Parser parser{"local function f(a) return function() return a, print end end"};
    const auto ast = parser.parse_chunk();
    REQUIRE(ast.has_value());

    const std::string text = cpplua::print_ast(*ast);
    const std::string dot = cpplua::print_ast_dot(*ast);
    const auto occurrences = [](std::string_view haystack, std::string_view needle) {
        std::size_t count = 0;
        for (auto at = haystack.find(needle); at != std::string_view::npos;
             at = haystack.find(needle, at + 1)) {
            ++count;
        }
        return count;
    };
    const auto lines = static_cast<std::size_t>(std::ranges::count(text, '\n'));
    CHECK(occurrences(dot, "[label=") == lines);
    // Every node but the root has exactly one arrow into it.
    CHECK(occurrences(dot, " -> ") == lines - 1);
}

// ---- Part 12: conformance sweep ----

TEST_CASE("G10 each extra target of an assignment is a level of nesting", "[parser]") {
    const auto targets = [](int count) {
        std::string out = "a";
        for (int i = 1; i < count; ++i) {
            out += ",a";
        }
        return out + " = 1";
    };
    // The statement is one level, each target after the first is one more,
    // like Lua's recursive restassign, and they are held while the values
    // are read, which is one more: 1 + 198 + 1 = 200 levels.
    CHECK_NOTHROW(ast_of(targets(199)));
    CHECK(parse_error(targets(200)) == "input:1: C stack overflow");
    // The levels are left after the statement, so the next one starts over.
    CHECK_NOTHROW(ast_of(targets(199) + "\n" + targets(199)));
}
