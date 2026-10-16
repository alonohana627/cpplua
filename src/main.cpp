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
#include <lua2/version.hpp>

namespace {

constexpr std::string_view usage = "usage: lua2 [-v | --version]\n"
                                   "       lua2 --tokens <file.lua>\n";

// Reads a whole file as raw bytes, or returns why it could not.
std::expected<std::string, std::string> read_file(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::unexpected("cannot open " + path);
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// `lua2 --tokens file.lua`: print every token of the file (lexer debugging).
int print_tokens(const std::string& path) {
    const auto source = read_file(path);
    if (!source) {
        std::println(stderr, "lua2: {}", source.error());
        return EXIT_FAILURE;
    }
    // Lua names a file chunk after its path in error messages.
    const auto dump = lua2::token_dump(*source, path);
    if (!dump) {
        std::println(stderr, "lua2: {}", dump.error());
        return EXIT_FAILURE;
    }
    std::print("{}", *dump);
    return EXIT_SUCCESS;
}

int run(std::span<char*> args) {
    if (args.size() <= 1 || (args.size() == 2 && (std::string_view{args[1]} == "-v" ||
                                                  std::string_view{args[1]} == "--version"))) {
        std::println("{}", lua2::version());
        return EXIT_SUCCESS;
    }
    if (args.size() == 3 && std::string_view{args[1]} == "--tokens") {
        return print_tokens(args[2]);
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
