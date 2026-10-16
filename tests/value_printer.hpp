#pragma once

#include <string>

#include <catch2/catch_tostring.hpp>

#include "value/value.hpp"

// Catch2 prints values with debug_text when a check fails.
template <>
struct Catch::StringMaker<lua2::Value> {
    static std::string convert(lua2::Value value) { return lua2::debug_text(value); }
};
