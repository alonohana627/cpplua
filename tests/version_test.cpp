#include <catch2/catch_test_macros.hpp>

#include <cpplua/version.hpp>

TEST_CASE("version banner carries the project version") {
    REQUIRE(cpplua::version() == "cpplua " CPPLUA_VERSION);
}
