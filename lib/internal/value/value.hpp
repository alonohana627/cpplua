#pragma once

#include <bit>
#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

#include <lua2/type.hpp>

namespace lua2 {

class String;     // value/string.hpp
class LuaClosure; // vm/function.hpp
class CClosure;   // vm/function.hpp
class Table;      // value/table.hpp
class Userdata;   // value/userdata.hpp
class State;      // vm/state.hpp
struct GcObject;  // gc/object.hpp

// A function written in C++ that Lua can call (Lua's lua_CFunction): its
// arguments are on the thread's stack above its frame's function slot, up to
// the top; it pushes its results and returns how many there are. (Errors
// from C++ functions come with stage 8.)
using CFunction = int (*)(State& state);

// What a Value holds, finer than Type: like Lua's variant tags, a number is
// an integer or a float (V1.3), false and true have their own tags, so
// booleans need no payload, and a string is short (interned) or long (V10.2),
// so equality knows how to compare without looking inside.
enum class Tag : std::uint8_t {
    Nil,
    False,
    True,
    Integer,
    Float,
    CFunction,     // a function written in C++ (Lua's light C function): not collectable
    LightUserdata, // a C++ pointer as a value (Lua's light userdata): not collectable
    ShortString,
    LongString,
    LuaClosure, // a function written in Lua
    CClosure,   // a C++ function with upvalues (Lua's C closure)
    Table,
    Userdata, // a block of memory with a metatable (Lua's full userdata)
};

// The largest tag (the VM packs two tags in a byte).
inline constexpr Tag last_tag = Tag::Userdata;

// Tags from here on hold a pointer to an object the collector manages.
inline constexpr Tag first_collectable_tag = Tag::ShortString;

// One Lua value: a tag and an 8-byte payload (docs/plan/values.md, part 0).
//
// Where many values are stored together (the stack, table arrays; stages 5
// and 6), tags and payloads go into separate arrays, 9 bytes per value; tag()
// and payload() take a value apart for that, and from_parts() puts it back
// together. A Value itself is the form one value has while it is being worked
// on: an operand, a result.
//
// The payload is raw bits, read through std::bit_cast, so there is no union
// and no way to read a member that was never written. Values are made only by
// the typed constructors below, and every accessor checks the tag in every
// build: asking a float for its integer stops the program with a message
// instead of returning garbage. Measured, the checks cost nothing, because the
// code that calls an accessor has just tested the tag, and the compiler
// removes the second test.
class Value {
public:
    // nil.
    constexpr Value() = default;

    static constexpr Value nil() { return {}; }

    static constexpr Value boolean(bool b) { return Value{b ? Tag::True : Tag::False, 0}; }

    static constexpr Value integer(std::int64_t i) {
        return Value{Tag::Integer, std::bit_cast<std::uint64_t>(i)};
    }

    static constexpr Value number(double d) {
        return Value{Tag::Float, std::bit_cast<std::uint64_t>(d)};
    }

    // A string made by a Heap (value/string.hpp, where this is defined).
    // The value refers to the string; the Heap owns it.
    static Value string(const String* s);

    // A Lua function (vm/function.hpp, where this is defined).
    static Value closure(const LuaClosure* f);

    // A C++ function.
    static Value c_function(CFunction f) {
        return Value{Tag::CFunction, std::bit_cast<std::uint64_t>(f)};
    }

    // A table (value/table.hpp, where this is defined).
    static Value table(const Table* t);

    // A C++ function with upvalues (vm/function.hpp).
    static Value c_closure(const CClosure* f);

    // A full userdata (value/userdata.hpp).
    static Value userdata(const Userdata* u);

    // A C++ pointer, compared by address (light userdata).
    static Value light_userdata(const void* p) {
        return Value{Tag::LightUserdata, std::bit_cast<std::uint64_t>(p)};
    }

    // Rebuilds a value from what tag() and payload() returned. For the
    // containers that store tags and payloads apart.
    static constexpr Value from_parts(Tag tag, std::uint64_t payload) {
        return Value{tag, payload};
    }

    [[nodiscard]] constexpr Tag tag() const { return tag_; }

    [[nodiscard]] constexpr std::uint64_t payload() const { return payload_; }

    // Where the payload is, so the VM can load it as the type it needs (a
    // double straight into a float register) from a constant.
    [[nodiscard]] const std::uint64_t* payload_address() const { return &payload_; }

    // The type as Lua sees it (V1.1): integers and floats are both numbers.
    [[nodiscard]] constexpr Type type() const {
        switch (tag_) {
        case Tag::Nil: return Type::Nil;
        case Tag::False:
        case Tag::True: return Type::Boolean;
        case Tag::Integer:
        case Tag::Float: return Type::Number;
        case Tag::ShortString:
        case Tag::LongString: return Type::String;
        case Tag::CFunction:
        case Tag::LuaClosure:
        case Tag::CClosure: return Type::Function;
        case Tag::Table: return Type::Table;
        case Tag::LightUserdata:
        case Tag::Userdata: return Type::Userdata;
        }
        return Type::Nil; // not reached: every Tag is handled above
    }

    [[nodiscard]] constexpr bool is_nil() const { return tag_ == Tag::Nil; }

    [[nodiscard]] constexpr bool is_boolean() const {
        return tag_ == Tag::False || tag_ == Tag::True;
    }

    [[nodiscard]] constexpr bool is_number() const {
        return tag_ == Tag::Integer || tag_ == Tag::Float;
    }

    [[nodiscard]] constexpr bool is_integer() const { return tag_ == Tag::Integer; }

    [[nodiscard]] constexpr bool is_float() const { return tag_ == Tag::Float; }

    [[nodiscard]] constexpr bool is_string() const {
        return tag_ == Tag::ShortString || tag_ == Tag::LongString;
    }

    [[nodiscard]] constexpr bool is_closure() const { return tag_ == Tag::LuaClosure; }

    [[nodiscard]] constexpr bool is_function() const {
        return tag_ == Tag::LuaClosure || tag_ == Tag::CFunction || tag_ == Tag::CClosure;
    }

    [[nodiscard]] constexpr bool is_c_closure() const { return tag_ == Tag::CClosure; }

    [[nodiscard]] constexpr bool is_userdata() const { return tag_ == Tag::Userdata; }

    [[nodiscard]] constexpr bool is_light_userdata() const { return tag_ == Tag::LightUserdata; }

    [[nodiscard]] constexpr bool is_c_function() const { return tag_ == Tag::CFunction; }

    [[nodiscard]] constexpr bool is_table() const { return tag_ == Tag::Table; }

    // Whether the payload points to an object the collector manages.
    [[nodiscard]] constexpr bool is_collectable() const { return tag_ >= first_collectable_tag; }

    // Only nil and false are falsy (V1.4).
    [[nodiscard]] constexpr bool is_falsy() const { return tag_ == Tag::Nil || tag_ == Tag::False; }

    [[nodiscard]] constexpr bool is_truthy() const { return !is_falsy(); }

    // The accessors. Each stops the program if the value holds something
    // else; test first with the is_ functions.

    [[nodiscard]] constexpr bool
    as_boolean(std::source_location caller = std::source_location::current()) const {
        if (!is_boolean()) [[unlikely]] {
            wrong_access("boolean", caller);
        }
        return tag_ == Tag::True;
    }

    [[nodiscard]] constexpr std::int64_t
    as_integer(std::source_location caller = std::source_location::current()) const {
        if (!is_integer()) [[unlikely]] {
            wrong_access("integer", caller);
        }
        return std::bit_cast<std::int64_t>(payload_);
    }

    [[nodiscard]] constexpr double
    as_float(std::source_location caller = std::source_location::current()) const {
        if (!is_float()) [[unlikely]] {
            wrong_access("float", caller);
        }
        return std::bit_cast<double>(payload_);
    }

    [[nodiscard]] const String*
    as_string(std::source_location caller = std::source_location::current()) const {
        if (!is_string()) [[unlikely]] {
            wrong_access("string", caller);
        }
        return std::bit_cast<const String*>(payload_);
    }

    [[nodiscard]] const LuaClosure*
    as_closure(std::source_location caller = std::source_location::current()) const {
        if (!is_closure()) [[unlikely]] {
            wrong_access("function", caller);
        }
        return std::bit_cast<const LuaClosure*>(payload_);
    }

    [[nodiscard]] CFunction
    as_c_function(std::source_location caller = std::source_location::current()) const {
        if (!is_c_function()) [[unlikely]] {
            wrong_access("C++ function", caller);
        }
        return std::bit_cast<CFunction>(payload_);
    }

    [[nodiscard]] const Table*
    as_table(std::source_location caller = std::source_location::current()) const {
        if (!is_table()) [[unlikely]] {
            wrong_access("table", caller);
        }
        return std::bit_cast<const Table*>(payload_);
    }

    [[nodiscard]] const CClosure*
    as_c_closure(std::source_location caller = std::source_location::current()) const {
        if (!is_c_closure()) [[unlikely]] {
            wrong_access("C++ closure", caller);
        }
        return std::bit_cast<const CClosure*>(payload_);
    }

    [[nodiscard]] Userdata*
    as_userdata(std::source_location caller = std::source_location::current()) const {
        if (!is_userdata()) [[unlikely]] {
            wrong_access("userdata", caller);
        }
        return std::bit_cast<Userdata*>(payload_);
    }

    [[nodiscard]] void*
    as_light_userdata(std::source_location caller = std::source_location::current()) const {
        if (!is_light_userdata()) [[unlikely]] {
            wrong_access("light userdata", caller);
        }
        return std::bit_cast<void*>(payload_);
    }

    // The collectable object a value refers to, for the collector.
    [[nodiscard]] GcObject*
    as_object(std::source_location caller = std::source_location::current()) const {
        if (!is_collectable()) [[unlikely]] {
            wrong_access("object", caller);
        }
        return std::bit_cast<GcObject*>(payload_);
    }

    // Same tag and same payload bits. This is *not* Lua's == (part 4): 1 and
    // 1.0 are not identical, and a NaN is identical to itself. For tests and
    // containers.
    [[nodiscard]] constexpr bool identical(Value other) const {
        return tag_ == other.tag_ && payload_ == other.payload_;
    }

private:
    constexpr Value(Tag tag, std::uint64_t payload) : tag_(tag), payload_(payload) {}

    // Reports a wrong accessor call (a bug in lua2, never in the Lua program)
    // and stops the program.
    [[noreturn]] void wrong_access(std::string_view wanted, std::source_location caller) const;

    Tag tag_ = Tag::Nil;
    std::uint64_t payload_ = 0;
};

static_assert(sizeof(Value) == 16);
static_assert(sizeof(const String*) == sizeof(std::uint64_t)); // a pointer fits the payload
static_assert(std::is_trivially_copyable_v<Value>);

// The name of the tag, for messages: "nil", "false", "true", "integer",
// "float", "short string", "long string".
std::string_view tag_name(Tag tag);

// A value as text for tests and debugging: nil, true, false, 42, 1.0, inf,
// "a\n". Floats always show that they are floats (1.0, not 1), and strings
// are quoted with unprintable bytes escaped. Lua's own conversion of numbers
// to strings is part 6.
std::string debug_text(Value value);

} // namespace lua2
