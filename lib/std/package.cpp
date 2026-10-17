// The package library and require: a port of Lua 5.4.9's loadlib.c
// (docs/final_docs/stdlib.md). Modules written in Lua load as in Lua.
// cpplua loads no C libraries (C++ code adds its functions through the
// C++ API instead), so the C searchers answer as a Lua built without
// dynamic libraries does.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/table.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

#ifdef _WIN32
constexpr std::string_view dir_separator = "\\"; // LUA_DIRSEP
constexpr std::string_view path_default =        // LUA_PATH_DEFAULT
    "!\\lua\\?.lua;!\\lua\\?\\init.lua;!\\?.lua;!\\?\\init.lua;"
    "!\\..\\share\\lua\\5.4\\?.lua;!\\..\\share\\lua\\5.4\\?\\init.lua;"
    ".\\?.lua;.\\?\\init.lua";
constexpr std::string_view cpath_default = // LUA_CPATH_DEFAULT
    R"(!\?.dll;!\..\lib\lua\5.4\?.dll;!\loadall.dll;.\?.dll)";
#else
constexpr std::string_view dir_separator = "/";
constexpr std::string_view path_default =
    "/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua;"
    "/usr/local/lib/lua/5.4/?.lua;/usr/local/lib/lua/5.4/?/init.lua;"
    "./?.lua;./?/init.lua";
constexpr std::string_view cpath_default =
    "/usr/local/lib/lua/5.4/?.so;/usr/local/lib/lua/5.4/loadall.so;./?.so";
#endif

// DLMSG: what a Lua without dynamic libraries says.
constexpr std::string_view no_dynamic_libraries =
    "dynamic libraries not enabled; check your Lua installation";

// luaL_gsub
std::string replace_all(std::string_view s, std::string_view from, std::string_view to) {
    std::string out;
    std::size_t at = 0;
    for (std::size_t found = 0; (found = s.find(from, at)) != std::string_view::npos;
         at = found + from.size()) {
        out.append(s.substr(at, found - at));
        out += to;
    }
    out.append(s.substr(at));
    return out;
}

// The package table: upvalue 1 of require and of the searchers.
Value package_table(const State& state) {
    return state.get(state.ci->func).as_c_closure()->upvalues()[0];
}

bool readable(const std::string& name) {
    std::FILE* f = std::fopen(name.c_str(), "r"); // NOLINT(cppcoreguidelines-owning-memory)
    if (f == nullptr) {
        return false;
    }
    std::fclose(f); // NOLINT(cppcoreguidelines-owning-memory)
    return true;
}

// searchpath: the first readable file of `path` for `name`, or the
// message listing the files tried ("no file 'a'\n\tno file 'b'").
std::expected<std::string, std::string> search_path(std::string_view name, std::string_view path,
                                                    std::string_view sep, std::string_view dirsep) {
    std::string module{name};
    if (!sep.empty() && name.contains(sep.front())) {
        module = replace_all(name, sep, dirsep);
    }
    const std::string names = replace_all(path, "?", module);
    std::size_t at = 0;
    while (at <= names.size()) {
        std::size_t end = names.find(';', at);
        if (end == std::string::npos) {
            end = names.size();
        }
        std::string file = names.substr(at, end - at);
        if (!file.empty() && readable(file)) {
            return file;
        }
        if (end == names.size()) {
            break;
        }
        at = end + 1;
    }
    return std::unexpected("no file '" + replace_all(names, ";", "'\n\tno file '") + "'");
}

int package_searchpath(State& state) {
    std::string_view name;
    std::string_view path;
    std::string_view sep;
    std::string_view dirsep;
    if (!check_view(state, 1, name) || !check_view(state, 2, path) ||
        !opt_view(state, 3, ".", sep) || !opt_view(state, 4, dir_separator, dirsep)) {
        return c_function_error;
    }
    const auto found = search_path(name, path, sep, dirsep);
    if (found) {
        push_string(state, *found);
        return 1;
    }
    push(state, Value::nil());
    push_string(state, found.error());
    return 2;
}

int package_loadlib(State& state) {
    std::string_view path;
    std::string_view init;
    if (!check_view(state, 1, path) || !check_view(state, 2, init)) {
        return c_function_error;
    }
    push(state, Value::nil());
    push_string(state, no_dynamic_libraries);
    push_string(state, "absent");
    return 3;
}

// findfile: the file for `name` in package[path_field].
bool find_file(State& state, std::string_view name, std::string_view path_field,
               std::expected<std::string, std::string>& out) {
    Value path;
    if (index_value(state, package_table(state), Value::string(state.heap().new_string(path_field)),
                    path) != Status::Ok) {
        return false;
    }
    if (!path.is_string()) {
        raise_error(state, std::format("'package.{}' must be a string", path_field));
        return false;
    }
    out = search_path(name, path.as_string()->view(), ".", dir_separator);
    return true;
}

int searcher_preload(State& state) {
    std::string_view name;
    if (!check_view(state, 1, name)) {
        return c_function_error;
    }
    Value preload;
    if (index_value(state, package_table(state), Value::string(state.heap().new_string("preload")),
                    preload) != Status::Ok) {
        return c_function_error;
    }
    Value loader;
    if (index_value(state, preload, arg(state, 1), loader) != Status::Ok) {
        return c_function_error;
    }
    if (loader.is_nil()) {
        push_string(state, std::format("no field package.preload['{}']", name));
        return 1;
    }
    push(state, loader);
    push_string(state, ":preload:");
    return 2;
}

int searcher_lua(State& state) {
    std::string_view name;
    if (!check_view(state, 1, name)) {
        return c_function_error;
    }
    std::expected<std::string, std::string> file;
    if (!find_file(state, name, "path", file)) {
        return c_function_error;
    }
    if (!file) {
        push_string(state, file.error());
        return 1;
    }
    const auto source = read_chunk_file(*file);
    std::expected<void, std::string> loaded;
    if (source) {
        loaded = load_chunk(state, *source, "@" + *file, "bt");
    } else {
        loaded = std::unexpected(source.error());
    }
    if (!loaded) { // checkload
        return raise_error(state, std::format("error loading module '{}' from file '{}':\n\t{}",
                                              name, *file, loaded.error()));
    }
    push_string(state, *file);
    return 2;
}

int searcher_c(State& state) {
    std::string_view name;
    if (!check_view(state, 1, name)) {
        return c_function_error;
    }
    std::expected<std::string, std::string> file;
    if (!find_file(state, name, "cpath", file)) {
        return c_function_error;
    }
    if (!file) {
        push_string(state, file.error());
        return 1;
    }
    return raise_error(state, std::format("error loading module '{}' from file '{}':\n\t{}", name,
                                          *file, no_dynamic_libraries));
}

int searcher_croot(State& state) {
    std::string_view name;
    if (!check_view(state, 1, name)) {
        return c_function_error;
    }
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos) {
        return 0; // a root
    }
    std::expected<std::string, std::string> file;
    if (!find_file(state, name.substr(0, dot), "cpath", file)) {
        return c_function_error;
    }
    if (!file) {
        push_string(state, file.error());
        return 1;
    }
    return raise_error(state, std::format("error loading module '{}' from file '{}':\n\t{}", name,
                                          *file, no_dynamic_libraries));
}

int require(State& state) {
    std::string_view name_view;
    if (!check_view(state, 1, name_view)) {
        return c_function_error;
    }
    state.top = arg_slot(state, 1) + 1; // lua_settop(L, 1)
    const std::string name{name_view};
    Heap& heap = state.heap();
    const Value loaded = Value::table(&heap.loaded());
    const Value key = arg(state, 1);
    Value module;
    if (index_value(state, loaded, key, module) != Status::Ok) {
        return c_function_error;
    }
    if (module.is_truthy()) {
        push(state, module); // already loaded
        return 1;
    }
    // findloader
    Value searchers;
    if (index_value(state, package_table(state), Value::string(heap.new_string("searchers")),
                    searchers) != Status::Ok) {
        return c_function_error;
    }
    if (!searchers.is_table()) {
        return raise_error(state, "'package.searchers' must be a table");
    }
    push(state, searchers); // anchored at slot 2
    std::string message;
    const std::uint32_t func = state.top;
    for (std::int64_t i = 1;; ++i) {
        const Value searcher = table_object(searchers)->get_int(i).value();
        if (searcher.is_nil()) {
            return raise_error(state, std::format("module '{}' not found:{}", name, message));
        }
        state.top = func;
        push(state, searcher);
        push(state, key);
        if (call(state, func, 2) != Status::Ok) {
            return c_function_error;
        }
        const Value result = state.get(func);
        if (result.is_function()) {
            break; // the loader, and its data at func + 1
        }
        if (result.is_string() || result.is_number()) {
            message += "\n\t";
            message += display_text(result);
        }
    }
    // Call the loader with the name and the loader data: [loader, data,
    // loader, name, data].
    const Value loader = state.get(func);
    const Value data = state.get(func + 1);
    const std::uint32_t call_at = state.top;
    push(state, loader);
    push(state, key);
    push(state, data);
    if (call(state, call_at, 1) != Status::Ok) {
        return c_function_error;
    }
    const Value result = state.get(call_at);
    state.top = call_at;
    if (!result.is_nil() && assign_value(state, loaded, key, result) != Status::Ok) {
        return c_function_error;
    }
    if (index_value(state, loaded, key, module) != Status::Ok) {
        return c_function_error;
    }
    if (module.is_nil()) { // the module set no value: true
        module = Value::boolean(true);
        if (assign_value(state, loaded, key, module) != Status::Ok) {
            return c_function_error;
        }
    }
    push(state, module);
    push(state, data);
    return 2;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

// The directory of the running program, for '!' in the paths (setprogdir).
// NOLINTNEXTLINE(modernize-use-string-view): a built string on Windows
std::string program_directory() {
#ifdef _WIN32
    std::array<char, MAX_PATH + 1> buffer{};
    const DWORD n = GetModuleFileNameA(nullptr, buffer.data(), MAX_PATH);
    const std::string path{buffer.data(), n};
    const std::size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
#else
    return {};
#endif
}

// setpath: package[field] from the environment (LUA_PATH_5_4, then
// LUA_PATH), where ";;" stands for the default; else the default, as also
// when `no_environment` (registry.LUA_NOENV, cpplua -E).
std::string initial_path(std::string_view variable, std::string_view fallback,
                         bool no_environment) {
    // NOLINTBEGIN(concurrency-mt-unsafe): read once, when the library opens
    const char* value = std::getenv(std::format("{}_5_4", variable).c_str());
    if (value == nullptr) {
        value = std::getenv(std::string{variable}.c_str());
    }
    // NOLINTEND(concurrency-mt-unsafe)
    std::string path;
    if (value == nullptr || no_environment) {
        path = fallback;
    } else {
        const std::string_view text{value};
        const std::size_t mark = text.find(";;");
        if (mark == std::string_view::npos) {
            path = text;
        } else {
            if (mark > 0) {
                path.append(text.substr(0, mark));
                path += ';';
            }
            path += fallback;
            if (mark < text.size() - 2) {
                path += ';';
                path.append(text.substr(mark + 2));
            }
        }
    }
#ifdef _WIN32
    path = replace_all(path, "!", program_directory());
#endif
    return path;
}

constexpr std::array package_functions{
    LibraryFunction{.name = "loadlib", .function = &package_loadlib},
    LibraryFunction{.name = "searchpath", .function = &package_searchpath},
};

} // namespace

void open_package(Heap& heap) {
    Table* package = open_library(heap, "package", package_functions);
    const Value package_value = Value::table(package);
    const auto set = [&](Table& t, std::string_view name, Value v) {
        t.set(heap, Value::string(heap.new_string(name)), v);
    };
    // The searchers, each with the package table as its upvalue.
    Table* searchers = heap.new_table();
    set(*package, "searchers", Value::table(searchers));
    constexpr std::array<CFunction, 4> searcher_functions{
        &searcher_preload,
        &searcher_lua,
        &searcher_c,
        &searcher_croot,
    };
    for (std::size_t i = 0; i < searcher_functions.size(); ++i) {
        CClosure* closure = heap.new_c_closure(searcher_functions.at(i), 1);
        closure->upvalues()[0] = package_value;
        searchers->set_int(heap, static_cast<std::int64_t>(i) + 1, Value::c_closure(closure));
    }
    // noenv: the stand-alone program's -E option
    const bool no_environment =
        heap.registry().get(Value::string(heap.new_string("LUA_NOENV"))).value().is_truthy();
    set(*package, "path",
        Value::string(heap.new_string(initial_path("LUA_PATH", path_default, no_environment))));
    set(*package, "cpath",
        Value::string(heap.new_string(initial_path("LUA_CPATH", cpath_default, no_environment))));
    set(*package, "config",
        Value::string(heap.new_string(std::format("{}\n;\n?\n!\n-\n", dir_separator))));
    set(*package, "loaded", Value::table(&heap.loaded()));
    const Table* preload = heap.new_table();
    set(*package, "preload", Value::table(preload));
    CClosure* require_closure = heap.new_c_closure(&require, 1);
    require_closure->upvalues()[0] = package_value;
    set(heap.globals(), "require", Value::c_closure(require_closure));
}

} // namespace cpplua
