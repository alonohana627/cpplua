#include <catch2/catch_test_macros.hpp>

#include <lua2/version.hpp>

TEST_CASE("version banner names Lua 5.4") {
  REQUIRE(lua2::version().starts_with("Lua 5.4"));
}
