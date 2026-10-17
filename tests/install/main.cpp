// tests/install/CMakeLists.txt: embeds cpplua through its installed headers
// and library only.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <cpplua/interpreter.hpp>
#include <cpplua/version.hpp>

int main() {
    cpplua::Interpreter lua;
    lua.set_function("twice", [](double x) { return 2 * x; });
    const auto results = lua.run("return twice(21), _VERSION", "consumer.lua");
    if (!results || results->size() != 2 || (*results)[0] != "42.0" || (*results)[1] != "Lua 5.4") {
        std::fputs("consumer: unexpected result\n", stderr);
        return EXIT_FAILURE;
    }
    std::printf("consumer OK (%s)\n", std::string{cpplua::version()}.c_str());
    return EXIT_SUCCESS;
}
