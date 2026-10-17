// The stand-alone interpreter: a port of Lua 5.4.9's lua.c
// (docs/final_docs/standalone.md). `cpplua file.lua args`, the options -e,
// -l, -i, -v, -E and -W, the `arg` table, LUA_INIT, and the interactive
// loop, as the real `lua` has them.

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include <cpplua/standalone.hpp>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <cpplua/version.hpp>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/table.hpp"
#include "vm/execute.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness): the CFunction signature

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): argv[0], as Lua's
const char* progname = "cpplua";

// lua_writestringerror and l_message
void error_message(std::string_view text) {
    std::cerr << progname << ": " << text << '\n';
    std::cerr.flush();
}

// report: the error object on the top, as text.
Status report(State& state, Status status) {
    if (status != Status::Ok) {
        const Value error = state.get(state.top - 1);
        error_message(error.is_string() || error.is_number() ? display_text(error)
                                                             : "(error message not a string)");
        --state.top;
    }
    return status;
}

// laction: Ctrl-C stops the running Lua code at its next instruction
// ("interrupted!").
extern "C" void on_interrupt(int /*signal*/) {
    std::signal(SIGINT, SIG_DFL); // a second Ctrl-C ends the program
    request_interrupt();
}

// docall: the function and its `nargs` arguments on the top, called with
// the traceback message handler.
Status docall(State& state, int nargs, int nresults) {
    const std::uint32_t func = state.top - static_cast<std::uint32_t>(nargs) - 1;
    std::signal(SIGINT, &on_interrupt);
    const Status status =
        protected_call(state, func, nresults, Value::c_function(&traceback_handler));
    std::signal(SIGINT, SIG_DFL);
    return status;
}

// dochunk, dofile, dostring: a loaded chunk (or its load error) run.
Status dochunk(State& state, const std::expected<void, std::string>& loaded) {
    Status status = Status::Ok;
    if (!loaded) {
        push_string(state, loaded.error());
        status = Status::Error;
    } else {
        status = docall(state, 0, 0);
    }
    return report(state, status);
}

std::expected<void, std::string> load_file(State& state, const std::string& name) {
    const auto source = read_chunk_file(name);
    if (!source) {
        return std::unexpected(source.error());
    }
    return load_chunk(state, *source, name.empty() ? "=stdin" : "@" + name, "bt");
}

Status dofile(State& state, const std::string& name) {
    return dochunk(state, load_file(state, name));
}

Status dostring(State& state, std::string_view text, std::string_view name) {
    return dochunk(state, load_chunk(state, text, name, "bt"));
}

// dolibrary: -l mod (global mod = require "mod"), -l g=mod, and "mod-v2"
// as global mod.
Status dolibrary(State& state, std::string_view spec) {
    std::string global{spec};
    std::string module{spec};
    if (const std::size_t eq = spec.find('='); eq != std::string_view::npos) {
        global = spec.substr(0, eq);
        module = spec.substr(eq + 1);
    } else if (const std::size_t mark = global.find('-'); mark != std::string::npos) {
        global.resize(mark); // LUA_IGMARK: what follows is a version
    }
    Heap& heap = state.heap();
    state.push(heap.globals().get(Value::string(heap.new_string("require"))).value());
    state.push(Value::string(heap.new_string(module)));
    const Status status = docall(state, 1, 1);
    if (status == Status::Ok) {
        heap.globals().set(heap, Value::string(heap.new_string(global)), state.get(state.top - 1));
        --state.top;
    }
    return report(state, status);
}

// The options (collectargs).
struct Options {
    bool error = false;
    bool interactive = false;    // -i
    bool version = false;        // -v (and -i)
    bool execute = false;        // -e
    bool no_environment = false; // -E
    int script = 0;              // the index of the script name, 0 for none
};

Options collect_options(std::span<char* const> argv) {
    Options o;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        o.script = static_cast<int>(i);
        const std::string_view a = argv[i];
        if (!a.starts_with('-')) {
            return o; // the script
        }
        const std::string_view rest = a.substr(1);
        if (rest.empty()) {
            return o; // "-": the script is stdin
        }
        switch (rest.front()) {
        case '-':
            if (rest.size() != 1) {
                o.error = true;
                return o;
            }
            o.script = i + 1 < argv.size() ? static_cast<int>(i) + 1 : 0;
            return o;
        case 'E':
        case 'W':
            if (rest.size() != 1) {
                o.error = true;
                return o;
            }
            o.no_environment = o.no_environment || rest.front() == 'E';
            break;
        case 'i':
        case 'v':
            if (rest.size() != 1) {
                o.error = true;
                return o;
            }
            o.interactive = o.interactive || rest.front() == 'i';
            o.version = true;
            break;
        case 'e':
        case 'l':
            o.execute = o.execute || rest.front() == 'e';
            if (rest.size() == 1) { // the argument is the next one
                ++i;
                if (i >= argv.size() || std::string_view{argv[i]}.starts_with('-')) {
                    o.error = true;
                    o.script = static_cast<int>(i) - 1;
                    return o;
                }
            }
            break;
        default: o.error = true; return o;
        }
    }
    o.script = 0;
    return o;
}

void print_usage(std::string_view bad) {
    std::cerr << progname << ": ";
    if (bad.size() > 1 && (bad[1] == 'e' || bad[1] == 'l')) {
        std::cerr << "'" << bad << "' needs argument\n";
    } else {
        std::cerr << "unrecognized option '" << bad << "'\n";
    }
    std::cerr << "usage: " << progname
              << " [options] [script [args]]\n"
                 "Available options are:\n"
                 "  -e stat   execute string 'stat'\n"
                 "  -i        enter interactive mode after executing 'script'\n"
                 "  -l mod    require library 'mod' into global 'mod'\n"
                 "  -l g=mod  require library 'mod' into global 'g'\n"
                 "  -v        show version information\n"
                 "  -E        ignore environment variables\n"
                 "  -W        turn warnings on\n"
                 "  --        stop handling options\n"
                 "  -         stop handling options and execute stdin\n";
    std::cerr.flush();
}

void print_version(Heap& heap) {
    heap.write_output(std::format("{}\n", version()));
}

// runargs: -e and -l in order, and -W.
bool run_arguments(State& state, std::span<char* const> argv, int limit) {
    for (int i = 1; i < limit; ++i) {
        const std::string_view a = argv[static_cast<std::size_t>(i)];
        const char option = a[1];
        if (option == 'e' || option == 'l') {
            std::string_view extra = a.substr(2);
            if (extra.empty()) {
                extra = argv[static_cast<std::size_t>(++i)];
            }
            const Status status =
                option == 'e' ? dostring(state, extra, "=(command line)") : dolibrary(state, extra);
            if (status != Status::Ok) {
                return false;
            }
        } else if (option == 'W') {
            state.heap().set_warnings_on(true);
        }
    }
    return true;
}

// handle_luainit: LUA_INIT_5_4 or LUA_INIT, a chunk or "@file".
Status run_init(State& state) {
    // NOLINTBEGIN(concurrency-mt-unsafe): read once, at the start
    std::string name = "=LUA_INIT_5_4";
    const char* init = std::getenv("LUA_INIT_5_4");
    if (init == nullptr) {
        name = "=LUA_INIT";
        init = std::getenv("LUA_INIT");
    }
    // NOLINTEND(concurrency-mt-unsafe)
    if (init == nullptr) {
        return Status::Ok;
    }
    const std::string_view text{init};
    if (text.starts_with('@')) {
        return dofile(state, std::string{text.substr(1)});
    }
    return dostring(state, text, name);
}

// handle_script: the script with its arguments (arg[1], arg[2], ...).
Status run_script(State& state, std::span<char* const> argv, int script) {
    std::string name = argv[static_cast<std::size_t>(script)];
    if (name == "-" && std::string_view{argv[static_cast<std::size_t>(script) - 1]} != "--") {
        name.clear(); // stdin
    }
    const auto loaded = load_file(state, name);
    if (!loaded) {
        push_string(state, loaded.error());
        return report(state, Status::Error);
    }
    const auto count = static_cast<int>(argv.size()) - script - 1;
    if (!state.check_stack(static_cast<std::uint32_t>(count) + 3)) {
        push_string(state, "too many arguments to script");
        return report(state, Status::Error);
    }
    Heap& heap = state.heap();
    for (int i = 1; i <= count; ++i) {
        state.push(Value::string(
            heap.new_string(argv[static_cast<std::size_t>(script) + static_cast<std::size_t>(i)])));
    }
    return report(state, docall(state, count, all_results));
}

// ---- The interactive loop (doREPL) ----

bool stdin_is_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

// get_prompt: _PROMPT and _PROMPT2, or "> " and ">> ".
std::string prompt(State& state, bool first) {
    Heap& heap = state.heap();
    const Value p =
        heap.globals().get(Value::string(heap.new_string(first ? "_PROMPT" : "_PROMPT2"))).value();
    if (p.is_nil()) {
        return first ? "> " : ">> ";
    }
    Value text;
    if (to_display_string(state, p, text) != Status::Ok) {
        return first ? "> " : ">> ";
    }
    return std::string{text.as_string()->view()};
}

bool read_line(State& state, bool first, std::string& line) {
    std::cout << prompt(state, first);
    std::cout.flush();
    if (!std::getline(std::cin, line)) {
        return false;
    }
    if (first && line.starts_with('=')) { // 5.2's "=expression"
        line = "return " + line.substr(1);
    }
    return true;
}

// incomplete: a syntax error at the end of the input.
bool incomplete(const std::string& message) {
    return message.ends_with("<eof>");
}

// loadline: a line as "return line;", else as a statement, joined with the
// next lines while it is incomplete.
std::expected<void, std::string> load_line(State& state, bool& end) {
    std::string line;
    if (!read_line(state, true, line)) {
        end = true;
        return {};
    }
    if (auto as_return = load_chunk(state, "return " + line + ";", "=stdin", "bt")) {
        return as_return;
    }
    for (;;) {
        auto loaded = load_chunk(state, line, "=stdin", "bt");
        if (loaded || !incomplete(loaded.error())) {
            return loaded;
        }
        std::string more;
        if (!read_line(state, false, more)) {
            return loaded;
        }
        line += '\n';
        line += more;
    }
}

void repl(State& state) {
    const char* const old_progname = progname;
    progname = "";
    for (;;) {
        const std::uint32_t base = state.top;
        bool end = false;
        const auto loaded = load_line(state, end);
        if (end) {
            break;
        }
        Status status = Status::Ok;
        if (!loaded) {
            push_string(state, loaded.error());
            status = Status::Error;
        } else {
            status = docall(state, 0, all_results);
        }
        if (status == Status::Ok && state.top > base) {
            // l_print: the results, through print.
            Heap& heap = state.heap();
            const Value print = heap.globals().get(Value::string(heap.new_string("print"))).value();
            state.push(Value::nil());
            for (std::uint32_t slot = state.top - 1; slot > base; --slot) {
                state.set(slot, state.get(slot - 1));
            }
            state.set(base, print);
            if (protected_call(state, base, 0, Value::nil()) != Status::Ok) {
                error_message(std::format("error calling 'print' ({})",
                                          display_text(state.get(state.top - 1))));
            }
        } else if (status != Status::Ok) {
            const Value error = state.get(state.top - 1);
            std::cerr << (error.is_string() || error.is_number() ? display_text(error)
                                                                 : "(error message not a string)")
                      << '\n';
        }
        state.top = base;
    }
    state.heap().write_output("\n");
    progname = old_progname;
}

// The body of pmain: true when everything ran without an error.
bool run(State& state, std::span<char* const> argv, bool generational) {
    const Options options = collect_options(argv);
    const int limit = options.script > 0 ? options.script : static_cast<int>(argv.size());
    Heap& heap = state.heap();
    if (options.error) {
        print_usage(argv[static_cast<std::size_t>(options.script)]);
        return false;
    }
    if (options.version) {
        print_version(heap);
    }
    if (options.no_environment) { // LUA_NOENV: libraries ignore the environment
        heap.registry().set(heap, Value::string(heap.new_string("LUA_NOENV")),
                            Value::boolean(true));
    }
    open_libraries(heap);
    // createargtable: the script at 0, its arguments after, the options before.
    Table* arg = heap.new_table();
    for (std::size_t i = 0; i < argv.size(); ++i) {
        arg->set_int(heap, static_cast<std::int64_t>(i) - options.script,
                     Value::string(heap.new_string(argv[i])));
    }
    heap.globals().set(heap, Value::string(heap.new_string("arg")), Value::table(arg));
    heap.set_mode(generational ? GcMode::Generational : GcMode::Incremental); // lua.c: generational
    if (!options.no_environment && run_init(state) != Status::Ok) {
        return false;
    }
    if (!run_arguments(state, argv, limit)) {
        return false;
    }
    if (options.script > 0 && run_script(state, argv, options.script) != Status::Ok) {
        return false;
    }
    if (options.interactive) {
        repl(state);
    } else if (options.script < 1 && !options.execute && !options.version) {
        if (stdin_is_terminal()) {
            print_version(heap);
            repl(state);
        } else {
            dofile(state, "");
        }
    }
    return true;
}

// What pmain gets (lua.c passes argc and argv on the stack).
struct MainArguments {
    std::span<char* const> argv;
    bool generational = true;
};

// pmain: everything runs inside this C++ function, called in protected
// mode, as in lua.c (so tracebacks end with "[C]: in ?", as lua's do).
int pmain(State& state) {
    const auto* arguments = static_cast<const MainArguments*>(arg(state, 1).as_light_userdata());
    push(state, Value::boolean(run(state, arguments->argv, arguments->generational)));
    return 1;
}

// NOLINTEND(misc-const-correctness)

} // namespace

int run_standalone(std::span<char* const> argv, bool generational) {
    if (!argv.empty() && argv[0] != nullptr && argv[0][0] != '\0') {
        progname = argv[0];
    }
    Heap heap{};
    State& state = heap.main_thread();
    const MainArguments arguments{.argv = argv, .generational = generational};
    const std::uint32_t func = state.top;
    state.push(Value::c_function(&pmain));
    state.push(Value::light_userdata(&arguments));
    const Status status = protected_call(state, func, 1, Value::nil());
    const bool ok = status == Status::Ok && state.get(state.top - 1).is_truthy();
    report(state, status);
    heap.close();
    std::cout.flush();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace cpplua
