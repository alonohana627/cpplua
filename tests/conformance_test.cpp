#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cpplua/debug.hpp>
#include <cpplua/source_file.hpp>

#include "lexer/lexer.hpp"

// Runs against the official Lua test suite in tests/fixtures/. CMake passes
// its location as CPPLUA_FIXTURES_DIR.

namespace {

const std::filesystem::path suite = CPPLUA_FIXTURES_DIR "/lua-5.4.9-tests";

std::vector<std::filesystem::path> lua_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{suite}) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
            files.push_back(entry.path());
        }
    }
    return files;
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// Counts lines the way the lexer does (L2.1): \r\n and \n\r are one break.
int count_lines(const std::string& text) {
    int lines = 1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\r') {
            const char next = i + 1 < text.size() ? text[i + 1] : '\0';
            if ((next == '\n' || next == '\r') && next != c) {
                ++i;
            }
            ++lines;
        }
    }
    return lines;
}

} // namespace

TEST_CASE("every file of the Lua 5.4.9 test suite lexes", "[conformance]") {
    const auto files = lua_files();
    REQUIRE(files.size() == 33);

    for (const auto& path : files) {
        const std::string name = path.filename().string();
        CAPTURE(name);
        const std::string source = read_bytes(path);
        cpplua::Lexer lexer{source, name};

        int last_line = 0;
        for (;;) {
            const auto token = lexer.next();
            if (!token) {
                FAIL(token.error().message);
            }
            if (token->kind == cpplua::TokenKind::EndOfStream) {
                last_line = token->line;
                break;
            }
        }
        // Every line break was counted: the end is on the file's last line.
        CHECK(last_line == count_lines(source));
    }
}

namespace {

// The files are loaded as files, like `lua file.lua` does: all.lua and
// main.lua start with a '#' line, which only the file loader skips.
std::string parsed_tree(const std::filesystem::path& path) {
    const std::string source = read_bytes(path);
    const auto tree = cpplua::ast_dump(cpplua::skip_file_prefix(source), path.filename().string());
    if (!tree) {
        FAIL(tree.error());
    }
    return *tree;
}

} // namespace

TEST_CASE("every file of the Lua 5.4.9 test suite parses", "[conformance]") {
    const auto files = lua_files();
    REQUIRE(files.size() == 33);

    for (const auto& path : files) {
        CAPTURE(path.filename().string());
        CHECK(parsed_tree(path).starts_with("Chunk\n"));
    }
}

TEST_CASE("every file of the Lua 5.4.9 test suite compiles", "[conformance][vm]") {
    // Stage 5's compiler on real programs. (Its listings were compared with
    // the real luac's, file by file; see docs/final_docs/vm.md.)
    for (const auto& path : lua_files()) {
        const std::string name = path.filename().string();
        CAPTURE(name);
        const std::string source = read_bytes(path);
        const auto listing = cpplua::bytecode_dump(cpplua::skip_file_prefix(source), name);
        REQUIRE(listing.has_value());
        CHECK(listing->starts_with("\nmain <" + name + ":0,0>"));
    }
}

TEST_CASE("spot checks of the test suite's trees", "[conformance]") {
    // all.lua: the '#!../lua' line is skipped, line 6 is the first statement.
    CHECK(parsed_tree(suite / "all.lua")
              .starts_with("Chunk\n"
                           "  Local version\n"
                           "    String \"Lua 5.4\"\n"));

    // main.lua: '# testing special comment on first line'.
    CHECK(parsed_tree(suite / "main.lua")
              .starts_with("Chunk\n"
                           "  If\n"
                           "    If\n"
                           "      Name _port\n"));

    // locals.lua, line 181: `local a<const>, b, c<const> = 10, 20, 30`.
    CHECK(parsed_tree(suite / "locals.lua").contains("Local a <const>, b, c <const>\n"));

    // goto.lua, lines 45-51: gotos and labels in and after a block.
    const std::string go = parsed_tree(suite / "goto.lua");
    CHECK(go.contains("  Do\n"
                      "    Local y\n"
                      "      Integer 12\n"
                      "    Goto l1\n"
                      "    Label l2\n"));
    CHECK(go.contains("\n  Label l3\n"
                      "  Label l3_1\n"));
    // goto.lua, line 164: a closure capturing four locals.
    CHECK(go.contains("Function() [upvalues: a, b, c, d]\n"));
}

namespace {

// errors.lua's testrep: a construct repeated 100 times must load, and 500
// times must not ("too many ..." or "... overflow").
std::string repeated(const char* init, const char* rep, const char* close, const char* repc,
                     int times) {
    std::string out = init;
    for (int i = 0; i < times; ++i) {
        out += rep;
    }
    out += close;
    for (int i = 0; i < times; ++i) {
        out += repc;
    }
    return out;
}

} // namespace

TEST_CASE("errors.lua: 100 levels of nesting load, 500 do not", "[conformance]") {
    struct Case {
        const char* init;
        const char* rep;
        const char* close;
        const char* repc;
    };

    // errors.lua, lines 650-659.
    const auto [init, rep, close, repc] = GENERATE(values<Case>({
        {"local a; a", ",a", "= 1", ",1"}, // multiple assignment
        {"local a; a=", "{", "0", "}"},
        {"return ", "(", "2", ")"},
        {"local function a (x) return x end; return ", "a(", "2.2", ")"},
        {"", "do ", "", " end"},
        {"", "while a do ", "", " end"},
        {"local a; ", "if a then else ", "", " end"},
        {"", "function foo () ", "", " end"},
        {"local a = ''; return ", "a..", "'a'", ""},
        {"local a = 1; return ", "a^", "a", ""},
    }));
    CAPTURE(rep);

    const auto ok = cpplua::ast_dump(repeated(init, rep, close, repc, 100));
    CHECK(ok.has_value());

    const auto too_deep = cpplua::ast_dump(repeated(init, rep, close, repc, 500));
    REQUIRE_FALSE(too_deep.has_value());
    CHECK((too_deep.error().contains("too many") || too_deep.error().contains("overflow")));
}
