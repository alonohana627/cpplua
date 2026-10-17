#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include "gc/object.hpp"
#include "runtime_error.hpp"
#include "value/value.hpp"

namespace cpplua {

class Heap; // gc/heap.hpp

// Strings of up to this many bytes are short: the Heap keeps one copy of each
// (V10.2). Lua's LUAI_MAXSHORTLEN.
inline constexpr std::size_t max_short_length = 40;

// Lua's string hash (V10.3, luaS_hash): from the seed and the length, then
// each byte from the last to the first.
constexpr std::uint32_t string_hash(std::string_view bytes, std::uint32_t seed) {
    auto h = seed ^ static_cast<std::uint32_t>(bytes.size());
    for (std::size_t i = bytes.size(); i > 0; --i) {
        h ^= (h << 5U) + (h >> 2U) + static_cast<unsigned char>(bytes[i - 1]);
    }
    return h;
}

// An immutable Lua string (V10.1): any bytes, '\0' included. Only a Heap
// (gc/heap.hpp) makes strings, and the collector frees them once nothing
// refers to them; values refer to them by pointer.
//
// The header and the bytes are one allocation: the bytes follow the header
// in memory, then a '\0' so that C functions can read them too. This is how
// Lua lays out its strings: one allocation per string, and the bytes next to
// the length.
class String : public GcObject {
public:
    String(const String&) = delete;
    String& operator=(const String&) = delete;
    String(String&&) = delete;
    String& operator=(String&&) = delete;
    ~String() = default;

    [[nodiscard]] std::size_t length() const { return length_; }

    // Short strings are interned (V10.2); long ones are not.
    [[nodiscard]] bool is_short() const { return type == ObjectType::ShortString; }

    // The bytes, followed in memory by a '\0'.
    [[nodiscard]] const char* data() const {
        // The bytes start right after the header (see Heap::allocate_string).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const char*>(this + 1);
    }

    [[nodiscard]] std::string_view view() const { return {data(), length_}; }

    // The hash (V10.3). A short string's was computed when it was made; a
    // long string's is computed the first time it is asked for, like Lua's
    // luaS_hashlongstr, since most long strings are never table keys.
    [[nodiscard]] std::uint32_t hash() const {
        if (!has_hash_) {
            hash_ = string_hash(view(), hash_); // hash_ held the seed until now
            has_hash_ = true;
        }
        return hash_;
    }

private:
    friend class Heap;

    String(std::size_t length, std::uint32_t hash, bool is_short)
        : length_(length), hash_(hash), has_hash_(is_short) {}

    String* next_in_bucket_ = nullptr; // short strings with the same bucket
    std::size_t length_;
    mutable std::uint32_t hash_;
    mutable bool has_hash_;
};

inline Value Value::string(const String* s) {
    return Value{s->is_short() ? Tag::ShortString : Tag::LongString,
                 std::bit_cast<std::uint64_t>(s)};
}

// #s (V9.3, V9.4): a string's length in bytes. Tables join in stage 6; any
// other value is `attempt to get length of a X value`.
std::expected<Value, RuntimeError> length(Value value);

// a .. b .. c (V9.1, V9.2): the values joined into one string, numbers
// written as in V6. `values` holds every operand of a chain of .., in order
// (Lua concatenates a whole chain at once). Anything but strings and numbers
// is `attempt to concatenate a X value`, naming the operand Lua's
// luaV_concat would. A single value is returned as it is.
std::expected<Value, RuntimeError> concatenate(Heap& heap, std::span<const Value> values);

} // namespace cpplua
