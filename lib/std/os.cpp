// The os library: a port of Lua 5.4.9's loslib.c
// (docs/final_docs/stdlib.md), on the C++ standard library: files through
// std::filesystem, the time through std::chrono, and <ctime>'s calendar
// functions where C++ has nothing else (it has no time zone database here).

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/convert.hpp"
#include "value/locale.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "vm/execute.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

// The conversions os.date accepts (LUA_STRFTIMEOPTIONS): one-character
// ones, then after "||" two-character ones.
#ifdef _WIN32
constexpr std::string_view strftime_options = "aAbBcdHIjmMpSUwWxXyYzZ%"
                                              "||"
                                              "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y";
#else
constexpr std::string_view strftime_options = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%"
                                              "||"
                                              "EcECExEXEyEY"
                                              "OdOeOHOIOmOMOSOuOUOVOwOWOy";
#endif

int os_execute(State& state) {
    std::optional<std::string> command;
    if (!arg_is_none_or_nil(state, 1)) {
        std::string_view text;
        if (!check_view(state, 1, text)) {
            return c_function_error;
        }
        command = std::string{text};
    }
    errno = 0;
    std::cout.flush();
    std::cerr.flush();
    // NOLINTNEXTLINE(bugprone-command-processor,concurrency-mt-unsafe,cert-env33-c): os.execute
    const int stat = std::system(command ? command->c_str() : nullptr);
    if (command) {
        return exec_result(state, stat);
    }
    push(state, Value::boolean(stat != 0)); // whether there is a shell
    return 1;
}

// luaL_fileresult for a std::filesystem error: its errno value and text.
int filesystem_result(State& state, const std::error_code& ec, const char* name) {
    if (!ec) {
        push(state, Value::boolean(true));
        return 1;
    }
    return error_result(state, ec.default_error_condition().value(), name);
}

// os.remove: a file or an empty folder, as C's remove.
int os_remove(State& state) {
    std::string_view name;
    if (!check_view(state, 1, name)) {
        return c_function_error;
    }
    const std::string file{name};
    std::error_code ec;
    if (!std::filesystem::remove(file, ec) && !ec) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
    }
    return filesystem_result(state, ec, file.c_str());
}

int os_rename(State& state) {
    std::string_view from;
    std::string_view to;
    if (!check_view(state, 1, from) || !check_view(state, 2, to)) {
        return c_function_error;
    }
    std::error_code ec;
    std::filesystem::rename(std::string{from}, std::string{to}, ec);
    return filesystem_result(state, ec, nullptr);
}

// os.tmpname: a new empty file in the temporary folder, as POSIX Lua's
// mkstemp makes one, so the name stays taken.
int os_tmpname(State& state) {
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::temp_directory_path(ec);
    static unsigned int counter = 0;
    const auto stamp =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int attempt = 0; !ec && attempt < 100; ++attempt) {
        const auto path = folder / std::format("lua_{:x}{:x}", stamp & 0xFFFFFFU, ++counter);
        if (std::filesystem::exists(path, ec) || ec) {
            continue;
        }
        if (std::ofstream{path}) { // create it
            push_string(state, path.string());
            return 1;
        }
    }
    return raise_error(state, "unable to generate a unique filename");
}

int os_getenv(State& state) {
    std::string_view name;
    if (!check_view(state, 1, name)) {
        return c_function_error;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): as Lua's
    const char* value = std::getenv(std::string{name}.c_str());
    if (value == nullptr) {
        push(state, Value::nil());
    } else {
        push_string(state, value);
    }
    return 1;
}

int os_clock(State& state) {
    // CPU time: C++ has no clock for it but <ctime>'s.
    push(state, Value::number(static_cast<double>(std::clock()) / CLOCKS_PER_SEC));
    return 1;
}

// ---- Dates ----

// setallfields: the fields of a date table.
bool set_all_fields(State& state, Value t, const std::tm& stm) {
    Heap& heap = state.heap();
    const auto set = [&](std::string_view key, Value v) {
        return assign_value(state, t, Value::string(heap.new_string(key)), v) == Status::Ok;
    };
    const auto set_int = [&](std::string_view key, int value, int delta) {
        return set(key, Value::integer(static_cast<std::int64_t>(value) + delta));
    };
    return set_int("year", stm.tm_year, 1900) && set_int("month", stm.tm_mon, 1) &&
           set_int("day", stm.tm_mday, 0) && set_int("hour", stm.tm_hour, 0) &&
           set_int("min", stm.tm_min, 0) && set_int("sec", stm.tm_sec, 0) &&
           set_int("yday", stm.tm_yday, 1) && set_int("wday", stm.tm_wday, 1) &&
           (stm.tm_isdst < 0 || set("isdst", Value::boolean(stm.tm_isdst != 0)));
}

// getfield: an integer field minus `delta`; `fallback` when absent (none
// if negative).
bool get_field(State& state, Value t, std::string_view key, int fallback, int delta, int& out) {
    Value v;
    if (index_value(state, t, Value::string(state.heap().new_string(key)), v) != Status::Ok) {
        return false;
    }
    std::optional<std::int64_t> n;
    if (v.is_string()) {
        if (const auto number = string_to_number(v.as_string()->view())) {
            n = to_integer(*number);
        }
    } else {
        n = to_integer(v);
    }
    if (!n) {
        if (!v.is_nil()) {
            raise_error(state, std::format("field '{}' is not an integer", key));
            return false;
        }
        if (fallback < 0) {
            raise_error(state, std::format("field '{}' missing in date table", key));
            return false;
        }
        out = fallback;
        return true;
    }
    const std::int64_t res = *n;
    if (!(res >= 0 ? res - delta <= INT_MAX : INT_MIN + delta <= res)) {
        raise_error(state, std::format("field '{}' is out-of-bound", key));
        return false;
    }
    out = static_cast<int>(res - delta);
    return true;
}

// getboolfield: -1 when absent.
bool get_bool_field(State& state, Value t, std::string_view key, int& out) {
    Value v;
    if (index_value(state, t, Value::string(state.heap().new_string(key)), v) != Status::Ok) {
        return false;
    }
    out = v.is_nil() ? -1 : static_cast<int>(v.is_truthy());
    return true;
}

// The calendar time of t, local or UTC (l_localtime, l_gmtime). The
// result of std::localtime is copied at once, as Lua does on systems
// without localtime_r.
bool local_time(std::time_t t, std::tm& out) {
    const std::tm* tm = std::localtime(&t); // NOLINT(concurrency-mt-unsafe): copied at once
    if (tm == nullptr) {
        return false;
    }
    out = *tm;
    return true;
}

bool utc_time(std::time_t t, std::tm& out) {
    const std::tm* tm = std::gmtime(&t); // NOLINT(concurrency-mt-unsafe): copied at once
    if (tm == nullptr) {
        return false;
    }
    out = *tm;
    return true;
}

// The time now, in seconds since the epoch (time(NULL)).
std::time_t now() {
    const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::time_t>(std::chrono::floor<std::chrono::seconds>(since_epoch).count());
}

// checkoption: the conversion at the start of `conv`, or an error.
std::optional<std::string_view> conversion(State& state, std::string_view conv) {
    std::size_t length = 1;
    for (std::size_t at = 0; at < strftime_options.size() && length <= conv.size(); at += length) {
        if (strftime_options[at] == '|') {
            ++length; // "||": the next block has longer options
        } else if (conv.substr(0, length) == strftime_options.substr(at, length)) {
            return conv.substr(0, length);
        }
    }
    arg_error(state, 1, std::format("invalid conversion specifier '%{}'", conv));
    return std::nullopt;
}

int os_date(State& state) {
    std::string_view s;
    if (!opt_view(state, 1, "%c", s)) {
        return c_function_error;
    }
    std::time_t t = 0;
    if (arg_is_none_or_nil(state, 2)) {
        t = now();
    } else {
        std::int64_t n = 0;
        if (!check_integer(state, 2, n)) {
            return c_function_error;
        }
        t = static_cast<std::time_t>(n);
    }
    std::tm stm{};
    bool ok = false;
    if (s.starts_with('!')) {
        ok = utc_time(t, stm);
        s.remove_prefix(1);
    } else {
        ok = local_time(t, stm);
    }
    if (!ok) {
        return raise_error(state, "date result cannot be represented in this installation");
    }
    if (s == "*t") {
        Table* table = state.heap().new_table();
        table->resize(state.heap(), 0, 9);
        push(state, Value::table(table));
        if (!set_all_fields(state, Value::table(table), stm)) {
            return c_function_error;
        }
        return 1;
    }
    std::string b;
    while (!s.empty()) {
        if (s.front() != '%') {
            b += s.front();
            s.remove_prefix(1);
            continue;
        }
        s.remove_prefix(1);
        const auto conv = conversion(state, s);
        if (!conv) {
            return c_function_error;
        }
        const std::string spec = "%" + std::string{*conv};
        std::array<char, 250> buffer{}; // SIZETIMEFMT
        const std::size_t n = std::strftime(buffer.data(), buffer.size(), spec.c_str(), &stm);
        b.append(buffer.data(), n);
        s.remove_prefix(conv->size());
    }
    push_string(state, b);
    return 1;
}

int os_time(State& state) {
    std::time_t t = 0;
    if (arg_is_none_or_nil(state, 1)) {
        t = now();
    } else {
        Table* table = nullptr;
        if (!check_table(state, 1, table)) {
            return c_function_error;
        }
        const Value v = arg(state, 1);
        std::tm ts{};
        if (!get_field(state, v, "year", -1, 1900, ts.tm_year) ||
            !get_field(state, v, "month", -1, 1, ts.tm_mon) ||
            !get_field(state, v, "day", -1, 0, ts.tm_mday) ||
            !get_field(state, v, "hour", 12, 0, ts.tm_hour) ||
            !get_field(state, v, "min", 0, 0, ts.tm_min) ||
            !get_field(state, v, "sec", 0, 0, ts.tm_sec) ||
            !get_bool_field(state, v, "isdst", ts.tm_isdst)) {
            return c_function_error;
        }
        t = std::mktime(&ts);
        if (!set_all_fields(state, v, ts)) { // the normalized values
            return c_function_error;
        }
    }
    if (t == static_cast<std::time_t>(-1)) {
        return raise_error(state, "time result cannot be represented in this installation");
    }
    push(state, Value::integer(static_cast<std::int64_t>(t)));
    return 1;
}

int os_difftime(State& state) {
    std::int64_t t1 = 0;
    std::int64_t t2 = 0;
    if (!check_integer(state, 1, t1) || !check_integer(state, 2, t2)) {
        return c_function_error;
    }
    push(state, Value::number(static_cast<double>(t1) - static_cast<double>(t2)));
    return 1;
}

int os_setlocale(State& state) {
    static constexpr std::array<int, 6> categories{
        LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY, LC_NUMERIC, LC_TIME,
    };
    static constexpr std::array<std::string_view, 6> names{
        "all", "collate", "ctype", "monetary", "numeric", "time",
    };
    std::optional<std::string> locale;
    if (!arg_is_none_or_nil(state, 1)) {
        std::string_view text;
        if (!check_view(state, 1, text)) {
            return c_function_error;
        }
        locale = std::string{text};
    }
    std::size_t op = 0;
    if (!check_option(state, 2, "all", names, op)) {
        return c_function_error;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): as Lua's
    const char* result = std::setlocale(categories.at(op), locale ? locale->c_str() : nullptr);
    refresh_locale(); // string order and numerals follow it (P4)
    if (result == nullptr) {
        push(state, Value::nil());
    } else {
        push_string(state, result);
    }
    return 1;
}

int os_exit(State& state) {
    int status = EXIT_SUCCESS;
    const Value code = arg(state, 1);
    if (code.is_boolean()) {
        status = code.is_truthy() ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        std::int64_t n = 0;
        if (!opt_integer(state, 1, EXIT_SUCCESS, n)) {
            return c_function_error;
        }
        status = static_cast<int>(n);
    }
    if (arg(state, 2).is_truthy()) {
        state.heap().close();
    }
    std::cout.flush();
    std::cerr.flush();
    std::exit(status); // NOLINT(concurrency-mt-unsafe): os.exit ends the program
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array os_functions{
    LibraryFunction{.name = "clock", .function = &os_clock},
    LibraryFunction{.name = "date", .function = &os_date},
    LibraryFunction{.name = "difftime", .function = &os_difftime},
    LibraryFunction{.name = "execute", .function = &os_execute},
    LibraryFunction{.name = "exit", .function = &os_exit},
    LibraryFunction{.name = "getenv", .function = &os_getenv},
    LibraryFunction{.name = "remove", .function = &os_remove},
    LibraryFunction{.name = "rename", .function = &os_rename},
    LibraryFunction{.name = "setlocale", .function = &os_setlocale},
    LibraryFunction{.name = "time", .function = &os_time},
    LibraryFunction{.name = "tmpname", .function = &os_tmpname},
};

} // namespace

void open_os(Heap& heap) {
    open_library(heap, "os", os_functions);
}

} // namespace cpplua
