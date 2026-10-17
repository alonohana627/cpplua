#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <lua2/debug.hpp>
#include <lua2/source_file.hpp>

#include "gc/heap.hpp"
#include "std/base.hpp"
#include "vm/run.hpp"

// Golden-file tests over tests/fixtures/my_lua/: every Lua file written while
// developing lua2 is kept there with its expected result next to it:
//
//   name.lua       the program
//   name.ast       the expected `lua2 --ast` output, if it parses
//   name.error     the expected error message, if it does not
//   name.dot       optionally, the expected `lua2 --ast=dot` output
//   name.bytecode  optionally, the expected `lua2 --bytecode` output (as the
//                  real luac -l -l lists it, without addresses)
//   name.out       optionally, what running it prints, then what it returns,
//                  as print shows it
//   name.runerror  optionally, the runtime error it stops with
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

std::filesystem::path with_extension(const std::filesystem::path& path, std::string_view ext) {
    return std::filesystem::path{path}.replace_extension(ext);
}

// Runs a program the way `lua2 file.lua` does, and returns its output: what
// it printed, then its results joined with tabs, as `print(...)` writes
// them (what `lua_ref -e "print(dofile('name.lua'))"` writes); or only the
// error message, with a final newline, like the golden files.
std::string run_output(std::string_view source, std::string_view name, lua2::GcMode mode,
                       lua2::GcStress stress) {
    lua2::Heap heap{1};
    lua2::open_base(heap);
    std::string out;
    heap.set_output([&out](std::string_view text) { out += text; });
    heap.set_mode(mode);
    heap.set_stress(stress);
    const auto results = lua2::run(heap.main_thread(), source, name);
    if (!results) {
        return results.error() + "\n";
    }
    for (std::size_t n = 0; n < results->size(); ++n) {
        if (n > 0) {
            out += '\t';
        }
        out += lua2::display_text((*results)[n]);
    }
    return out + "\n";
}

} // namespace

TEST_CASE("every file in my_lua parses to its golden output", "[my_lua]") {
    const auto files = lua_files();
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        const std::string name = path.filename().string();
        CAPTURE(name);
        const auto ast_path = with_extension(path, ".ast");
        const auto error_path = with_extension(path, ".error");
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

        const auto dot_path = with_extension(path, ".dot");
        if (std::filesystem::exists(dot_path)) {
            const auto dot = lua2::ast_dot_dump(lua2::skip_file_prefix(source), name);
            REQUIRE(dot.has_value());
            CHECK(*dot == read_bytes(dot_path));
        }
    }
}

TEST_CASE("every file in my_lua that parses also compiles", "[my_lua][vm]") {
    for (const auto& path : lua_files()) {
        if (!std::filesystem::exists(with_extension(path, ".ast"))) {
            continue;
        }
        const std::string name = path.filename().string();
        CAPTURE(name);
        const std::string source = read_bytes(path);
        const auto listing = lua2::bytecode_dump(lua2::skip_file_prefix(source), name);
        REQUIRE(listing.has_value());
        const auto bytecode_path = with_extension(path, ".bytecode");
        if (std::filesystem::exists(bytecode_path)) {
            CHECK(*listing == read_bytes(bytecode_path));
        }
    }
}

TEST_CASE("my_lua programs run to their golden results", "[my_lua][vm]") {
    for (const auto& path : lua_files()) {
        const auto out_path = with_extension(path, ".out");
        const auto runerror_path = with_extension(path, ".runerror");
        const bool has_out = std::filesystem::exists(out_path);
        const bool has_runerror = std::filesystem::exists(runerror_path);
        if (!has_out && !has_runerror) {
            continue;
        }
        const std::string name = path.filename().string();
        CAPTURE(name);
        REQUIRE(has_out != has_runerror);
        const std::string expected = read_bytes(has_out ? out_path : runerror_path);
        const std::string source = read_bytes(path);
        CHECK(run_output(lua2::skip_file_prefix(source), name, lua2::GcMode::Incremental,
                         lua2::GcStress::Off) == expected);
        CHECK(run_output(lua2::skip_file_prefix(source), name, lua2::GcMode::Generational,
                         lua2::GcStress::Off) == expected);
    }
}

TEST_CASE("E9.2 my_lua programs give the same results under GC stress", "[my_lua][vm][gc]") {
    // A collection at every place one can happen finds a missing root or
    // barrier the normal pacing would hide (docs/plan/vm.md).
    for (const auto& path : lua_files()) {
        const auto out_path = with_extension(path, ".out");
        const auto runerror_path = with_extension(path, ".runerror");
        if (!std::filesystem::exists(out_path) && !std::filesystem::exists(runerror_path)) {
            continue;
        }
        const std::string name = path.filename().string();
        CAPTURE(name);
        const std::string expected =
            read_bytes(std::filesystem::exists(out_path) ? out_path : runerror_path);
        const std::string source = read_bytes(path);
        const std::string_view program = lua2::skip_file_prefix(source);
        for (const auto mode : {lua2::GcMode::Incremental, lua2::GcMode::Generational}) {
            for (const auto stress : {lua2::GcStress::Steps, lua2::GcStress::FullCycles}) {
                CAPTURE(static_cast<int>(mode), static_cast<int>(stress));
                CHECK(run_output(program, name, mode, stress) == expected);
            }
        }
    }
}
