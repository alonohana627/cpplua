#pragma once

#include <bit>
#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

namespace lua2 {

// The eight types of Lua (V1.1), in the order of Lua's LUA_T* constants.
// Stage 4 makes values of the first four; the others come with their stages.
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

// What a Value holds, finer than Type: like Lua's variant tags, a number is
// an integer or a float (V1.3), and false and true have their own tags, so
// booleans need no payload.
enum class Tag : std::uint8_t {
    Nil,
    False,
    True,
    Integer,
    Float,
};

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

    // Rebuilds a value from what tag() and payload() returned. For the
    // containers that store tags and payloads apart.
    static constexpr Value from_parts(Tag tag, std::uint64_t payload) {
        return Value{tag, payload};
    }

    [[nodiscard]] constexpr Tag tag() const { return tag_; }

    [[nodiscard]] constexpr std::uint64_t payload() const { return payload_; }

    // The type as Lua sees it (V1.1): integers and floats are both numbers.
    [[nodiscard]] constexpr Type type() const {
        switch (tag_) {
        case Tag::Nil: return Type::Nil;
        case Tag::False:
        case Tag::True: return Type::Boolean;
        case Tag::Integer:
        case Tag::Float: return Type::Number;
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
static_assert(std::is_trivially_copyable_v<Value>);

// The name of the tag, for messages: "nil", "false", "true", "integer",
// "float".
std::string_view tag_name(Tag tag);

// A value as text for tests and debugging: nil, true, false, 42, 1.0, inf.
// Floats always show that they are floats (1.0, not 1). Lua's own conversion
// of numbers to strings is part 6.
std::string debug_text(Value value);

} // namespace lua2
