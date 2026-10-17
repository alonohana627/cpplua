#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <cpplua/interpreter.hpp>

// The standard library (stage 9) against the rules of docs/spec/stdlib.md.
// The my_lua programs in tests/fixtures/my_lua/stdlib/ check most of the
// behavior against the real Lua's output; these cover what a golden file
// cannot: files on disk, modules, and the reference files in std/.

namespace {

std::string run(cpplua::Interpreter& interpreter, std::string_view source) {
    const auto results = interpreter.run(source, "input");
    if (!results) {
        return results.error();
    }
    std::string out;
    for (std::size_t n = 0; n < results->size(); ++n) {
        if (n > 0) {
            out += ',';
        }
        out += (*results)[n];
    }
    return out;
}

std::string run(std::string_view source) {
    cpplua::Interpreter interpreter;
    return run(interpreter, source);
}

// Whether files opened in text mode write CR LF for a newline, as
// Windows's C library does (and the real Lua's files with it).
#ifdef _WIN32
constexpr bool text_mode_newlines = true;
#else
constexpr bool text_mode_newlines = false;
#endif

// A fresh empty folder under the system's temporary folder.
std::filesystem::path temp_folder(std::string_view name) {
    auto folder = std::filesystem::temp_directory_path() / "cpplua_stdlib_test" / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary};
    file << text;
}

// A path as Lua source text: forward slashes, in quotes.
std::string lua_string(const std::filesystem::path& path) {
    return "'" + path.generic_string() + "'";
}

} // namespace

TEST_CASE("S1.1 every library is open in an interpreter", "[stdlib]") {
    CHECK(run("return type(string), type(table), type(math), type(utf8), type(io), type(os), "
              "type(package), type(debug), type(require)") ==
          "table,table,table,table,table,table,table,table,function");
    CHECK(run("return package.loaded.string == string, package.loaded._G == _G, "
              "package.loaded.package == package") == "true,true,true");
}

TEST_CASE("S1.2 the reference files in std/ declare exactly the C++ functions", "[stdlib]") {
    // Every `function name(` and `name = ` line of std/*.lua, against every
    // field of the libraries, the globals that are not libraries, and the
    // methods of files.
    const std::set<std::string> libraries{
        "string", "table", "math", "utf8", "io", "os", "package", "debug", "coroutine",
    };
    std::set<std::string> declared;
    const std::regex function_line{R"(^function ([A-Za-z_][\w.:]*)\()"};
    const std::regex field_line{R"(^([A-Za-z_][\w.]*) = )"};
    for (const auto& entry : std::filesystem::directory_iterator{CPPLUA_SOURCE_DIR "/std"}) {
        if (entry.path().extension() != ".lua") {
            continue;
        }
        std::ifstream file{entry.path()};
        std::string line;
        while (std::getline(file, line)) {
            std::smatch m;
            if ((std::regex_search(line, m, function_line) ||
                 std::regex_search(line, m, field_line)) &&
                !libraries.contains(m[1].str())) { // `string = {}` declares the table
                declared.insert(m[1].str());
            }
        }
    }
    const std::string names = run(R"(
        local names = {}
        local libraries = {"string", "table", "math", "utf8", "io", "os", "package", "debug", "coroutine"}
        local is_library = {}
        for _, name in ipairs(libraries) do
            is_library[name] = true
            for key in pairs(_G[name]) do names[#names + 1] = name .. "." .. key end
        end
        for key, value in pairs(_G) do
            if not is_library[key] then names[#names + 1] = key end
        end
        for key in pairs(getmetatable(io.stdout).__index) do names[#names + 1] = "file:" .. key end
        table.sort(names)
        return table.concat(names, " "))");
    std::string expected;
    for (const auto& name : declared) {
        expected += (expected.empty() ? "" : " ") + name;
    }
    CHECK(names == expected);
}

TEST_CASE("S2.5 string.dump makes Lua 5.4 binary chunks", "[stdlib]") {
    CHECK(run("local f = load(string.dump(function(a) return a * 2, 'k' end)) return f(21)") ==
          "42,k");
    CHECK(run("local d = string.dump(function() return 1 end, true) "
              "return d:sub(1, 4) == '\\27Lua', #d < #string.dump(function() return 1 end)") ==
          "true,true");
    CHECK(run("return pcall(string.dump, print)") == "false,unable to dump given function");
    CHECK(run("return pcall(string.dump, 1)")
              .starts_with("false,bad argument #1 to 'string.dump' (function expected"));
}

TEST_CASE("S4.1 math.random gives the real Lua's numbers for a seed", "[stdlib]") {
    // What the real Lua 5.4.9 gives: math.randomseed(42), math.random(1,
    // 100) three times, then math.random(0) and a float.
    CHECK(run("math.randomseed(42) return math.random(1, 100), math.random(1, 100), "
              "math.random(1, 100), math.random(0), math.random()") ==
          "50,76,86,-7059243630996204619,0.0096006865506838");
    CHECK(run("return math.randomseed(7, 3)") == "7,3");
    CHECK(run("math.randomseed(1) local x = math.random() return x >= 0 and x < 1") == "true");
}

TEST_CASE("S6.1 files: write, read back by every format, seek, lines", "[stdlib]") {
    const auto folder = temp_folder("io");
    const std::string path = lua_string(folder / "data.txt");
    cpplua::Interpreter interpreter;
    CHECK(run(interpreter, "local f = assert(io.open(" + path +
                               ", 'w')) f:write('12 3.5 0x10\\n', 'line two\\n', 42, ' ', 1.5, "
                               "'\\nlast') return f:close()") == "true");
    CHECK(run(interpreter, "local f = io.open(" + path +
                               ") local a, b, c = f:read('n', 'n', 'n') local rest = f:read('l') "
                               "local l2 = f:read('L') local all = f:read('a') "
                               "local eof = f:read('l') f:close() "
                               "return a, b, c, rest, l2, all, eof") ==
          "12,3.5,16,,line two\n,42 1.5\nlast,nil");
    CHECK(run(interpreter, "local f = io.open(" + path +
                               ") local size = f:seek('end') f:seek('set', 3) "
                               "local s = f:read(3) f:close() return size, s") ==
          (text_mode_newlines ? "35,3.5" : "32,3.5"));
    CHECK(run(interpreter,
              "local n = 0 for l in io.lines(" + path + ") do n = n + 1 end return n") == "4");
    CHECK(run(interpreter, "local t = {} for a, b in io.lines(" + path +
                               ", 1, 2) do t[#t + 1] = a .. '|' .. b end return t[1], t[2]") ==
          "1|2 ,3|.5");
    CHECK(run(interpreter, "local f = io.open(" + path +
                               ") f:close() return io.type(f), tostring(f), "
                               "pcall(f.read, f)") ==
          "closed file,file (closed),false,attempt to use a closed file");
    CHECK(run(interpreter, "return io.open('no/such/file.txt')")
              .starts_with("nil,no/such/file.txt: "));
    CHECK(run(interpreter, "return pcall(io.open, 'x', 'rw')") ==
          "false,bad argument #2 to 'io.open' (invalid mode)");
}

TEST_CASE("S6.2 the standard streams write through the interpreter's output", "[stdlib]") {
    std::string out;
    cpplua::Interpreter interpreter;
    interpreter.set_output([&out](std::string_view text) { out += text; });
    CHECK(run(interpreter, "io.write('a', 1, 2.0, '\\n') io.stdout:write('b') "
                           "return io.type(io.stdout), io.stdout:close()") ==
          "file,nil,cannot close standard file");
    CHECK(out == "a12\nb");
}

TEST_CASE("S6.3 io.input and io.output change the default files", "[stdlib]") {
    const auto folder = temp_folder("default");
    const std::string path = lua_string(folder / "out.txt");
    cpplua::Interpreter interpreter;
    CHECK(run(interpreter,
              std::format("local old = io.output() io.output({0}) io.write('x', 'y') "
                          "io.close() io.output(old) io.input({0}) local s = io.read('a') "
                          "io.input():close() return s, pcall(io.read)",
                          path)) == "xy,false,default input file is closed");
}

TEST_CASE("S7.1 os.time and os.date agree", "[stdlib]") {
    CHECK(run("local t = os.time({year = 2024, month = 2, day = 29, hour = 13, min = 5, sec = 7}) "
              "local d = os.date('*t', t) return d.year, d.month, d.day, d.hour, d.min, d.sec, "
              "os.date('%Y-%m-%d %H:%M:%S', t)") == "2024,2,29,13,5,7,2024-02-29 13:05:07");
    CHECK(run("return os.date('!%H', 0), os.difftime(10, 4)") == "00,6.0");
    CHECK(run("return pcall(os.date, '%Ez')")
              .starts_with(
                  "false,bad argument #1 to 'os.date' (invalid conversion specifier '%Ez')"));
}

TEST_CASE("S7.2 os.remove, os.rename and os.getenv", "[stdlib]") {
    const auto folder = temp_folder("os");
    const std::string a = lua_string(folder / "a.txt");
    const std::string b = lua_string(folder / "b.txt");
    write_file(folder / "a.txt", "x");
    CHECK(run(std::format("return os.rename({0}, {1}), io.open({0}) == nil, os.remove({1})", a,
                          b)) == "true,true,true");
    CHECK(run("return os.remove(" + b + ")").ends_with("b.txt: No such file or directory,2"));
    CHECK(run("return os.getenv('CPPLUA_SURELY_NOT_SET_VARIABLE')") == "nil");
    CHECK(run("local name = os.tmpname() local f = io.open(name) local ok = f ~= nil "
              "f:close() return ok, os.remove(name)") == "true,true");
}

TEST_CASE("S6.4 io.popen is not supported", "[stdlib]") {
    CHECK(run("return pcall(io.popen, 'echo x')") == "false,'popen' not supported");
    CHECK(run("return pcall(io.popen, 'echo x', 'rw')") ==
          "false,bad argument #2 to 'io.popen' (invalid mode)");
}

TEST_CASE("S6.5 io.tmpfile is a file for update, removed when closed", "[stdlib]") {
    CHECK(run("local f = io.tmpfile() f:write('abc', 12) f:seek('set') local s = f:read('a') "
              "return s, f:close(), io.type(f)") == "abc12,true,closed file");
}

TEST_CASE("S8.1 load: strings, readers, modes and environments", "[stdlib]") {
    CHECK(run("return load('return 1 + 1')()") == "2");
    CHECK(run("local parts = {'return ', '4', '2'} local i = 0 "
              "return load(function() i = i + 1 return parts[i] end)()") == "42");
    CHECK(run("return load('x = ', 'name')") ==
          "nil,[string \"name\"]:1: unexpected symbol near <eof>");
    CHECK(run("return load('return x', 'chunk', 't', {x = 5})()") == "5");
    CHECK(run("return load('return 1', 'chunk', 'b')") ==
          "nil,attempt to load a text chunk (mode is 'b')");
    CHECK(run("return load('\\27Lua')") ==
          "nil,binary string: bad binary format (truncated chunk)");
    CHECK(run("return load(function() error('stop') end)") == "nil,input:1: stop");
    CHECK(run("return load(function() return {} end)") ==
          "nil,input:1: reader function must return a string");
}

TEST_CASE("S8.2 loadfile and dofile", "[stdlib]") {
    const auto folder = temp_folder("load");
    write_file(folder / "chunk.lua",
               "#!/usr/bin/env lua\nlocal a, b = ... return (a or 1) + (b or 2), x\n");
    write_file(folder / "bad.lua", "error('inside')\n");
    const std::string chunk = lua_string(folder / "chunk.lua");
    const std::string bad = lua_string(folder / "bad.lua");
    CHECK(run("return loadfile(" + chunk + ")(10, 20)") == "30,nil");
    CHECK(run("return loadfile(" + chunk + ", 't', {x = 'env'})()") == "3,env");
    CHECK(run("return dofile(" + chunk + ")") == "3,nil");
    // The chunk name is cut to its last 60 characters, as Lua's luaO_chunkid does.
    CHECK(run("return pcall(dofile, " + bad + ")").ends_with("load/bad.lua:1: inside"));
    CHECK(
        run("return loadfile('no_such_file.lua')").starts_with("nil,cannot open no_such_file.lua"));
}

TEST_CASE("S9.1 require loads Lua modules once, from package.path", "[stdlib]") {
    const auto folder = temp_folder("require");
    std::filesystem::create_directories(folder / "pkg");
    write_file(folder / "counter.lua",
               "count = (count or 0) + 1 return {name = ..., file = select(2, ...)}\n");
    write_file(folder / "pkg" / "init.lua", "return 'package init'\n");
    write_file(folder / "nothing.lua", "loaded_nothing = true\n");
    write_file(folder / "broken.lua", "return +\n");
    cpplua::Interpreter interpreter;
    const std::string dir = folder.generic_string();
    run(interpreter, "package.path = '" + dir + "/?.lua;" + dir + "/?/init.lua'");
    CHECK(run(interpreter, "local m = require('counter') return m.name, m.file, count") ==
          "counter," + dir + "/counter.lua,1");
    CHECK(run(interpreter, "require('counter') return count, package.loaded.counter.name") ==
          "1,counter");
    CHECK(run(interpreter, "return require('pkg')") == "package init," + dir + "/pkg/init.lua");
    CHECK(run(interpreter, "local m, file = require('nothing') return m, file, loaded_nothing") ==
          "true," + dir + "/nothing.lua,true");
    const std::string broken = run(interpreter, "return pcall(require, 'broken')");
    CHECK(broken.starts_with("false,error loading module 'broken' from file '" + dir +
                             "/broken.lua':\n\t"));
    CHECK(broken.ends_with("/broken.lua:1: unexpected symbol near '+'"));
    CHECK(run(interpreter,
              "package.preload.virtual = function(name, extra) return name .. extra end "
              "return require('virtual')") == "virtual:preload:,:preload:");
}

TEST_CASE("S9.2 cpplua loads no C libraries", "[stdlib]") {
    CHECK(run("return package.loadlib('x.dll', 'f')") ==
          "nil,dynamic libraries not enabled; check your Lua installation,absent");
    const auto folder = temp_folder("clib");
    write_file(folder / "native.dll", "");
    const std::string dir = folder.generic_string();
    CHECK(run("package.path = '' package.cpath = '" + dir +
              "/?.dll' return pcall(require, 'native')") ==
          "false,error loading module 'native' from file '" + dir +
              "/native.dll':\n\tdynamic libraries not enabled; check your Lua installation");
    CHECK(
        run("package.path = '' package.cpath = '' return pcall(require, 'x')") ==
        "false,module 'x' not found:\n\tno field package.preload['x']\n\tno file ''\n\tno file ''");
}

TEST_CASE("S10.1 warn writes only when switched on", "[stdlib]") {
    std::string out;
    cpplua::Interpreter interpreter;
    interpreter.set_error_output([&out](std::string_view text) { out += text; });
    run(interpreter, "warn('not shown') warn('@on') warn('shown ', 'in ', 'parts') warn('@off') "
                     "warn('hidden')");
    CHECK(out == "Lua warning: shown in parts\n");
}

TEST_CASE("S6.9 a directory cannot be read as a file", "[stdlib]") {
    // (POSIX opens a directory; Windows refuses to, in Lua too.)
    const auto folder = temp_folder("directory");
    const std::string path = lua_string(folder);
#ifndef _WIN32
    CHECK(run("local f = io.open(" + path + ") return f:read('a')") == "nil,Is a directory,21");
    CHECK(run("local _, e = loadfile(" + path + ") return e") ==
          "cannot read " + folder.generic_string() + ": Is a directory");
#else
    CHECK(run("return io.open(" + path + ") == nil") == "true");
    CHECK(run("return loadfile(" + path + ") == nil") == "true");
#endif
}

TEST_CASE("S6.10 os.remove and os.rename follow the C library of the system", "[stdlib]") {
    const auto folder = temp_folder("remove_rename");
    write_file(folder / "a.txt", "a");
    write_file(folder / "b.txt", "b");
    std::filesystem::create_directory(folder / "d");
    const std::string a = lua_string(folder / "a.txt");
    const std::string b = lua_string(folder / "b.txt");
    const std::string d = lua_string(folder / "d");
#ifdef _WIN32
    // rename never replaces a file, remove never deletes a folder
    CHECK(run("return os.rename(" + a + ", " + b + ")") == "nil,File exists,17");
    CHECK(run("return select(3, os.remove(" + d + "))") == "13");
#else
    CHECK(run("return os.rename(" + a + ", " + b + ")") == "true");
    CHECK(run("return os.remove(" + d + ")") == "true");
#endif
}
