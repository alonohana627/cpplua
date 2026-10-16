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

using lua2::Parser;

namespace {

// Parses `source` and returns the printed tree, failing the test on an error.
std::string ast_of(std::string_view source) {
    Parser parser{source, "input"};
    auto ast = parser.parse_chunk();
    if (!ast) {
        FAIL("unexpected parse error: " << ast.error().message);
    }
    return lua2::print_ast(*ast);
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
    using lua2::expected_text;
    using lua2::TokenKind;

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
    STATIC_CHECK_FALSE(std::is_convertible_v<lua2::ExprId, lua2::StatId>);
    STATIC_CHECK_FALSE(std::is_convertible_v<lua2::StatId, lua2::ExprId>);
    STATIC_CHECK(sizeof(lua2::StatId) == 4);
}

TEST_CASE("statement lists are stored back to back", "[parser][ast]") {
    using lua2::StatId;
    lua2::Ast ast;

    const std::array first{StatId{.index = 7}, StatId{.index = 8}};
    const std::array second{StatId{.index = 9}};
    const auto first_range = ast.add_statement_list(first);
    const auto second_range = ast.add_statement_list(second);
    const auto empty_range = ast.add_statement_list({});

    CHECK(first_range == lua2::IdRange<StatId>{.first = 0, .count = 2});
    CHECK(second_range == lua2::IdRange<StatId>{.first = 2, .count = 1});
    CHECK(empty_range.count == 0);

    const auto read_back = ast.statements(lua2::Block{.statements = first_range});
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

    const auto& node = std::get<lua2::ReturnStat>(ast->statement(statements[0]));
    const auto values = ast->expressions(node.values);
    REQUIRE(values.size() == 2);
    CHECK(ast->line(values[0]) == 3);
    CHECK(ast->line(values[1]) == 5);
    CHECK(std::get<lua2::IntegerExpr>(ast->expression(values[0])).value == 1);
    CHECK(ast->text(std::get<lua2::StringExpr>(ast->expression(values[1])).text) == "x");
}

TEST_CASE("string values are stored once in the shared text buffer", "[parser][ast]") {
    lua2::Ast ast;
    const auto first = ast.add_text("hello");
    const auto second = ast.add_text("");
    const auto third = ast.add_text("world");

    CHECK(first == lua2::TextRange{.first = 0, .length = 5});
    CHECK(second.length == 0);
    CHECK(third == lua2::TextRange{.first = 5, .length = 5});
    CHECK(ast.text(first) == "hello");
    CHECK(ast.text(third) == "world");
}

// ---- Part 2: operators and precedence ----

namespace {

std::string grouped_list(const lua2::Ast& ast, lua2::IdRange<lua2::ExprId> list);

// Parses `source` and returns its single statement, failing the test on an
// error or on any other number of statements.
lua2::Stat only_statement(std::string_view source, lua2::Ast& ast) {
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
std::string grouped(const lua2::Ast& ast, lua2::ExprId id) {
    return std::visit(
        lua2::Overloaded{
            [](const lua2::NilExpr&) -> std::string { return "nil"; },
            [](const lua2::TrueExpr&) -> std::string { return "true"; },
            [](const lua2::FalseExpr&) -> std::string { return "false"; },
            [](const lua2::VarargExpr&) -> std::string { return "..."; },
            [](const lua2::IntegerExpr& node) { return std::to_string(node.value); },
            [](const lua2::FloatExpr& node) { return lua2::format_float(node.value); },
            [&](const lua2::StringExpr& node) {
                return "'" + std::string(ast.text(node.text)) + "'";
            },
            [&](const lua2::UnaryExpr& node) {
                const auto op = lua2::operator_text(node.op);
                const std::string_view separator = op == "not" ? " " : "";
                return "(" + std::string(op) + separator + grouped(ast, node.operand) + ")";
            },
            [&](const lua2::BinaryExpr& node) {
                return "(" + grouped(ast, node.left) + " " +
                       std::string(lua2::operator_text(node.op)) + " " + grouped(ast, node.right) +
                       ")";
            },
            [&](const lua2::NameExpr& node) { return std::string(ast.text(node.name)); },
            // Lua's own parentheses are shown as [ ] to tell them from the grouping.
            [&](const lua2::ParenExpr& node) { return "[" + grouped(ast, node.inner) + "]"; },
            [&](const lua2::IndexExpr& node) {
                return grouped(ast, node.object) + "{" + grouped(ast, node.key) + "}";
            },
            [&](const lua2::CallExpr& node) {
                return grouped(ast, node.function) + "(" + grouped_list(ast, node.arguments) + ")";
            },
            [&](const lua2::MethodCallExpr& node) {
                return grouped(ast, node.object) + ":" + std::string(ast.text(node.method)) + "(" +
                       grouped_list(ast, node.arguments) + ")";
            },
            // Tables as {item, name=value, [key]=value}.
            [&](const lua2::TableExpr& node) {
                std::string out = "{";
                for (const auto& field : ast.fields(node.fields)) {
                    out += out.size() == 1 ? "" : ", ";
                    switch (field.kind) {
                    case lua2::TableField::Kind::Positional: break;
                    case lua2::TableField::Kind::Named:
                        out += std::string(ast.text(
                                   std::get<lua2::StringExpr>(ast.expression(field.key)).text)) +
                               "=";
                        break;
                    case lua2::TableField::Kind::Keyed:
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

std::string grouped_list(const lua2::Ast& ast, lua2::IdRange<lua2::ExprId> list) {
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
        ast->expressions(std::get<lua2::ReturnStat>(ast->statement(statement)).values);
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
        ast->expressions(std::get<lua2::ReturnStat>(ast->statement(statement)).values);
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
                                            "    Name x\n");
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
                                               "        Name x\n");
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
    lua2::Ast ast;
    CHECK(std::get<lua2::IfStat>(only_statement("if a then else end", ast)).has_else);
    CHECK_FALSE(std::get<lua2::IfStat>(only_statement("if a then end", ast)).has_else);
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
                                                     "          Name i\n");
    lua2::Ast ast;
    CHECK_FALSE(
        std::get<lua2::NumericForStat>(only_statement("for i = 1, 2 do end", ast)).has_step);
    CHECK(std::get<lua2::NumericForStat>(only_statement("for i = 1, 2, 3 do end", ast)).has_step);
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
