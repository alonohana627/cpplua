#pragma once

#include <string>

#include <catch2/catch_tostring.hpp>

#include "value/value.hpp"

// Catch2 prints values with debug_text when a check fails.
template <>
struct Catch::StringMaker<cpplua::Value> {
    static std::string convert(cpplua::Value value) { return cpplua::debug_text(value); }
};
