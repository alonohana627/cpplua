#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cpplua/debug.hpp>
#include <cpplua/source_file.hpp>
#include <cpplua/standalone.hpp>

namespace {

// The debug views: each turns source text into printable text or an error.
using Dump = std::expected<std::string, std::string> (*)(std::string_view, std::string_view);

// Reads a whole file as raw bytes, or returns why it could not.
std::expected<std::string, std::string> read_file(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::unexpected("cannot open " + path);
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// `cpplua --tokens file.lua`, `cpplua --ast file.lua` and `--ast=dot`: print a debug view of
// the file, or the error that stopped it.
int print_dump(const std::string& path, Dump dump_view) {
    const auto source = read_file(path);
    if (!source) {
        std::println(stderr, "cpplua: {}", source.error());
        return EXIT_FAILURE;
    }
    // Lua names a file chunk after its path in error messages.
    const auto dump = dump_view(cpplua::skip_file_prefix(*source), path);
    if (!dump) {
        std::println(stderr, "cpplua: {}", dump.error());
        return EXIT_FAILURE;
    }
    std::print("{}", *dump);
    return EXIT_SUCCESS;
}

// cpplua's own options come first: a debug view of a file, or the collector's
// mode; everything else is the real `lua`'s command line (cpplua/standalone.hpp).
int run(std::span<char*> args) {
    if (args.size() == 3) {
        const std::string_view option = args[1];
        if (option == "--tokens") {
            return print_dump(args[2], &cpplua::token_dump);
        }
        if (option == "--ast") {
            return print_dump(args[2], &cpplua::ast_dump);
        }
        if (option == "--ast=dot") {
            return print_dump(args[2], &cpplua::ast_dot_dump);
        }
        if (option == "--bytecode") {
            return print_dump(args[2], &cpplua::bytecode_dump);
        }
    }
    if (args.size() >= 2 && (std::string_view{args[1]} == "--incremental" ||
                             std::string_view{args[1]} == "--generational")) {
        const bool generational = std::string_view{args[1]} == "--generational";
        std::vector<char*> rest{args[0]};
        rest.insert(rest.end(), args.begin() + 2, args.end());
        return cpplua::run_standalone(rest, generational);
    }
    if (args.size() == 2 && std::string_view{args[1]} == "--version") {
        args[1][1] = 'v'; // the same as -v
        args[1][2] = '\0';
    }
    return cpplua::run_standalone(args);
}

} // namespace

int main(int argc, char* argv[]) {
    // Nothing may escape main: an uncaught exception calls std::terminate.
    // The handlers use C stdio because std::println itself can throw.
    try {
        return run(std::span{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& e) {
        std::fputs("cpplua: ", stderr);
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("cpplua: unknown error\n", stderr);
    }
    return EXIT_FAILURE;
}
