#include <catch2/catch_test_macros.hpp>

#include <cpplua/version.hpp>

TEST_CASE("version banner carries the project version") {
    // Like `lua -v`: "Lua 5.4.9  Copyright (C) 1994-2026 Lua.org, PUC-Rio".
    REQUIRE(cpplua::version().starts_with("cpplua " CPPLUA_VERSION " (Lua 5.4.9)  Copyright (C) "));
    REQUIRE(cpplua::version().ends_with("Lua Copyright (C) 1994-2026 Lua.org, PUC-Rio"));
}
