#pragma once

#include <cstdint>
#include <string_view>

namespace cpplua {

// The eight types of Lua (V1.1), in the order of Lua's LUA_T* constants.
enum class Type : std::uint8_t {
    Nil,
    Boolean,
    Number,
    String,
    Table,
    Function,
    Userdata,
    Thread,
};

// The name of a type as Lua spells it (V1.2): what `type(v)` returns and what
// error messages say, as in "attempt to perform arithmetic on a nil value".
constexpr std::string_view type_name(Type type) {
    switch (type) {
    case Type::Nil: return "nil";
    case Type::Boolean: return "boolean";
    case Type::Number: return "number";
    case Type::String: return "string";
    case Type::Table: return "table";
    case Type::Function: return "function";
    case Type::Userdata: return "userdata";
    case Type::Thread: return "thread";
    }
    return "?"; // not reached: every Type is handled above
}

} // namespace cpplua
