// The benchmark runner (docs/plan/vm.md): runs every .lua file of a folder
// with cpplua and, when given, with the reference Lua, and prints the best wall
// time of each and the ratio, and whether both printed the same.
//
//   cpplua_bench <folder> <cpplua command> [<reference command>] [runs]
//
// A command is a path to an interpreter that takes the file to run as its
// argument. A program that fails is reported as failing, not timed. The
// time of starting the shell and the interpreter, measured on an empty
// program, is taken off each time.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Timing {
    double ms = 0;      // the best wall time, in milliseconds
    std::string output; // what the program printed
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// The best wall time of `runs` runs of `interpreter file`, and its output,
// or nothing if a run failed.
std::optional<Timing> best_time(const std::string& interpreter, const std::filesystem::path& file,
                                int runs) {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cpplua_bench_output.txt";
#ifdef _WIN32
    // Quoted twice: cmd.exe strips the outer quotes of a command line.
    const std::string command =
        "\"\"" + interpreter + "\" \"" + file.string() + "\" >\"" + output.string() + "\" 2>NUL\"";
#else
    const std::string command =
        "'" + interpreter + "' '" + file.string() + "' >'" + output.string() + "' 2>/dev/null";
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
    return Timing{.ms = best, .output = read_file(output)};
}

std::string shown(const std::optional<Timing>& t) {
    return t ? std::format("{:9.1f} ms", t->ms) : std::string{"    fails"};
}

// The time to start the shell and `interpreter` on an empty program.
double startup(const std::string& interpreter, int runs) {
    const std::filesystem::path empty =
        std::filesystem::temp_directory_path() / "cpplua_bench_empty.lua";
    std::ofstream{empty}.flush();
    const auto t = best_time(interpreter, empty, runs);
    return t ? t->ms : 0;
}

int run(std::span<char*> args) {
    if (args.size() < 3) {
        std::println(stderr, "usage: cpplua_bench <folder> <cpplua> [<reference lua>] [runs]");
        return EXIT_FAILURE;
    }
    const std::filesystem::path folder = args[1];
    const std::string cpplua = args[2];
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

    const double our_startup = startup(cpplua, runs);
    const double their_startup = reference.empty() ? 0 : startup(reference, runs);
    std::println("{:<22} {:>12} {:>12} {:>8}", "benchmark", "cpplua", "reference", "ratio");
    bool all_same = true;
    for (const auto& file : files) {
        auto ours = best_time(cpplua, file, runs);
        auto theirs = reference.empty() ? std::nullopt : best_time(reference, file, runs);
        if (ours) {
            ours->ms = std::max(ours->ms - our_startup, 0.0);
        }
        if (theirs) {
            theirs->ms = std::max(theirs->ms - their_startup, 0.0);
        }
        const std::string ratio = ours && theirs && theirs->ms > 0
                                      ? std::format("{:7.2f}x", ours->ms / theirs->ms)
                                      : std::string{"       -"};
        const bool differs = ours && theirs && ours->output != theirs->output;
        all_same = all_same && !differs;
        std::println("{:<22} {:>12} {:>12} {:>8}{}", file.stem().string(), shown(ours),
                     reference.empty() ? std::string{"           -"} : shown(theirs), ratio,
                     differs ? "  output differs!" : "");
    }
    std::println("(ratio = cpplua time / reference time; below 1 means cpplua is faster;");
    std::println(" startup, {:.1f} ms and {:.1f} ms, taken off)", our_startup, their_startup);
    return all_same ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(std::span{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& e) {
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("cpplua_bench: unknown error\n", stderr);
    }
    return EXIT_FAILURE;
}
