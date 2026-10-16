#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "lexer/lexer.hpp"

// Runs against the official Lua test suite in tests/fixtures/. CMake passes
// its location as LUA2_FIXTURES_DIR.

namespace {

std::vector<std::filesystem::path> lua_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator{LUA2_FIXTURES_DIR "/lua-5.4.9-tests"}) {
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
        lua2::Lexer lexer{source, name};

        int last_line = 0;
        for (;;) {
            const auto token = lexer.next();
            if (!token) {
                FAIL(token.error().message);
            }
            if (token->kind == lua2::TokenKind::EndOfStream) {
                last_line = token->line;
                break;
            }
        }
        // Every line break was counted: the end is on the file's last line.
        CHECK(last_line == count_lines(source));
    }
}
