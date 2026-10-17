// The benchmark runner (docs/plan/vm.md): runs every .lua file of a folder
// with lua2 and, when given, with the reference Lua, and prints the best wall
// time of each and the ratio.
//
//   lua2_bench <folder> <lua2 command> [<reference command>] [runs]
//
// A command is a path to an interpreter that takes the file to run as its
// argument. A program that fails is reported as failing, not timed.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The best wall time of `runs` runs of `interpreter file`, in milliseconds,
// or nothing if a run failed.
std::optional<double> best_time(const std::string& interpreter, const std::filesystem::path& file,
                                int runs) {
#ifdef _WIN32
    // Quoted twice: cmd.exe strips the outer quotes of a command line.
    const std::string command = "\"\"" + interpreter + "\" \"" + file.string() + "\" >NUL 2>&1\"";
#else
    const std::string command = "'" + interpreter + "' '" + file.string() + "' >/dev/null 2>&1";
#endif
    double best = 0;
    for (int run = 0; run < runs; ++run) {
        const auto start = std::chrono::steady_clock::now();
        // Runs the interpreters the user named, on our own benchmark files.
        // NOLINTNEXTLINE(cert-env33-c,bugprone-command-processor)
        const int status = std::system(command.c_str());
        const auto end = std::chrono::steady_clock::now();
        if (status != 0) {
            return std::nullopt;
        }
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        best = run == 0 ? ms : std::min(best, ms);
    }
    return best;
}

std::string shown(const std::optional<double>& ms) {
    return ms ? std::format("{:9.1f} ms", *ms) : std::string{"    fails"};
}

int run(std::span<char*> args) {
    if (args.size() < 3) {
        std::println(stderr, "usage: lua2_bench <folder> <lua2> [<reference lua>] [runs]");
        return EXIT_FAILURE;
    }
    const std::filesystem::path folder = args[1];
    const std::string lua2 = args[2];
    const std::string reference = args.size() > 3 ? std::string{args[3]} : std::string{};
    int runs = 5;
    if (args.size() > 4) {
        const std::string_view text = args[4];
        std::from_chars(text.data(), text.data() + text.size(), runs);
        runs = std::max(1, runs);
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator{folder}) {
        if (entry.path().extension() == ".lua") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);

    std::println("{:<22} {:>12} {:>12} {:>8}", "benchmark", "lua2", "reference", "ratio");
    for (const auto& file : files) {
        const auto ours = best_time(lua2, file, runs);
        const auto theirs = reference.empty() ? std::nullopt : best_time(reference, file, runs);
        const std::string ratio =
            ours && theirs ? std::format("{:7.2f}x", *ours / *theirs) : std::string{"       -"};
        std::println("{:<22} {:>12} {:>12} {:>8}", file.stem().string(), shown(ours),
                     reference.empty() ? std::string{"           -"} : shown(theirs), ratio);
    }
    std::println("(ratio = lua2 time / reference time; below 1 means lua2 is faster)");
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(std::span{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& e) {
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("lua2_bench: unknown error\n", stderr);
    }
    return EXIT_FAILURE;
}
