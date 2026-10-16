#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <lua2/debug.hpp>
#include <lua2/source_file.hpp>

// Golden-file tests over tests/fixtures/my_lua/: every Lua file written while
// developing lua2 is kept there with its expected result next to it:
//
//   name.lua    the program
//   name.ast    the expected `lua2 --ast` output, if it parses
//   name.error  the expected error message, if it does not
//   name.dot    optionally, the expected `lua2 --ast=dot` output
//
// The chunk name in error messages is the file name. See
// tests/fixtures/my_lua/README.md.

namespace {

const std::filesystem::path folder = LUA2_FIXTURES_DIR "/my_lua";

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::vector<std::filesystem::path> lua_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator{folder}) {
        if (entry.path().extension() == ".lua") {
            files.push_back(entry.path());
        }
    }
    return files;
}

} // namespace

TEST_CASE("every file in my_lua parses to its golden output", "[my_lua]") {
    const auto files = lua_files();
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        const std::string name = path.filename().string();
        CAPTURE(name);
        const auto ast_path = std::filesystem::path{path}.replace_extension(".ast");
        const auto error_path = std::filesystem::path{path}.replace_extension(".error");
        const bool has_ast = std::filesystem::exists(ast_path);
        const bool has_error = std::filesystem::exists(error_path);
        // Exactly one golden file per program.
        REQUIRE(has_ast != has_error);

        const std::string source = read_bytes(path); // a file, so loaded like Lua loads files
        const auto result = lua2::ast_dump(lua2::skip_file_prefix(source), name);
        if (has_ast) {
            REQUIRE(result.has_value());
            CHECK(*result == read_bytes(ast_path));
        } else {
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error() + "\n" == read_bytes(error_path));
        }

        const auto dot_path = std::filesystem::path{path}.replace_extension(".dot");
        if (std::filesystem::exists(dot_path)) {
            const auto dot = lua2::ast_dot_dump(lua2::skip_file_prefix(source), name);
            REQUIRE(dot.has_value());
            CHECK(*dot == read_bytes(dot_path));
        }
    }
}
