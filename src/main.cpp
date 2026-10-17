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

#include <lua2/debug.hpp>
#include <lua2/interpreter.hpp>
#include <lua2/source_file.hpp>
#include <lua2/version.hpp>

namespace {

constexpr std::string_view usage = "usage: lua2 [-v | --version]\n"
                                   "       lua2 [--generational] <file.lua>\n"
                                   "       lua2 --bytecode <file.lua>\n"
                                   "       lua2 --tokens <file.lua>\n"
                                   "       lua2 --ast <file.lua>\n"
                                   "       lua2 --ast=dot <file.lua>\n";

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

// `lua2 --tokens file.lua`, `lua2 --ast file.lua` and `--ast=dot`: print a debug view of
// the file, or the error that stopped it.
int print_dump(const std::string& path, Dump dump_view) {
    const auto source = read_file(path);
    if (!source) {
        std::println(stderr, "lua2: {}", source.error());
        return EXIT_FAILURE;
    }
    // Lua names a file chunk after its path in error messages.
    const auto dump = dump_view(lua2::skip_file_prefix(*source), path);
    if (!dump) {
        std::println(stderr, "lua2: {}", dump.error());
        return EXIT_FAILURE;
    }
    std::print("{}", *dump);
    return EXIT_SUCCESS;
}

// `lua2 file.lua`: runs the file (its `print`s write to stdout), then prints
// what it returns, if anything, separated by tabs, as `print(...)` would. An
// error goes to stderr with a stack traceback, as the real lua prints it.
int run_file(const std::string& path, bool generational) {
    const auto source = read_file(path);
    if (!source) {
        std::println(stderr, "lua2: {}", source.error());
        return EXIT_FAILURE;
    }
    lua2::Interpreter interpreter;
    interpreter.set_generational(generational);
    const auto results = interpreter.run(lua2::skip_file_prefix(*source), path, true);
    if (!results) {
        std::println(stderr, "lua2: {}", results.error());
        return EXIT_FAILURE;
    }
    if (!results->empty()) {
        std::string line;
        for (std::size_t n = 0; n < results->size(); ++n) {
            if (n > 0) {
                line += '	';
            }
            line += (*results)[n];
        }
        std::println("{}", line);
    }
    return EXIT_SUCCESS;
}

int run(std::span<char*> args) {
    if (args.size() <= 1 || (args.size() == 2 && (std::string_view{args[1]} == "-v" ||
                                                  std::string_view{args[1]} == "--version"))) {
        std::println("{}", lua2::version());
        return EXIT_SUCCESS;
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--tokens") {
        return print_dump(args[2], &lua2::token_dump);
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--ast") {
        return print_dump(args[2], &lua2::ast_dump);
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--ast=dot") {
        return print_dump(args[2], &lua2::ast_dot_dump);
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--bytecode") {
        return print_dump(args[2], &lua2::bytecode_dump);
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--generational") {
        return run_file(args[2], true);
    }
    if (args.size() == 2 && !std::string_view{args[1]}.starts_with('-')) {
        return run_file(args[1], false);
    }
    std::print(stderr, "{}", usage);
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[]) {
    // Nothing may escape main: an uncaught exception calls std::terminate.
    // The handlers use C stdio because std::println itself can throw.
    try {
        return run(std::span{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& e) {
        std::fputs("lua2: ", stderr);
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("lua2: unknown error\n", stderr);
    }
    return EXIT_FAILURE;
}
