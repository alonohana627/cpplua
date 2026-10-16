#include <catch2/catch_test_macros.hpp>

#include <lua2/version.hpp>

TEST_CASE("version banner carries the project version") {
    REQUIRE(lua2::version() == "lua2 " LUA2_VERSION);
}
