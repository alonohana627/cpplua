#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "runtime_error.hpp"
#include "value/value.hpp"

namespace lua2 {

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
// makes strings, and it owns them; values refer to them by pointer.
//
// The header and the bytes are one allocation: the bytes follow the header
// in memory, then a '\0' so that C functions can read them too. This is how
// Lua lays out its strings: one allocation per string, and the bytes next to
// the length.
class String {
public:
    String(const String&) = delete;
    String& operator=(const String&) = delete;
    String(String&&) = delete;
    String& operator=(String&&) = delete;
    ~String() = default;

    [[nodiscard]] std::size_t length() const { return length_; }

    [[nodiscard]] bool is_short() const { return is_short_; }

    // The bytes, followed in memory by a '\0'.
    [[nodiscard]] const char* data() const {
        // The bytes start right after the header (see Heap::new_string).
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
        : length_(length), hash_(hash), is_short_(is_short), has_hash_(is_short) {}

    String* next_object_ = nullptr;    // every string of the Heap, to free them
    String* next_in_bucket_ = nullptr; // short strings with the same bucket
    std::size_t length_;
    mutable std::uint32_t hash_;
    bool is_short_;
    mutable bool has_hash_;
};

// Owns every string, until the garbage collector (stage 9) takes its place:
// strings live as long as their Heap, and the Heap frees them all when it is
// destroyed. Short strings are interned: making "abc" twice gives the same
// String both times (V10.2). Like Lua's string table (lstring.c), they are
// found through an array of buckets, each a chain of strings, which doubles
// when it holds as many strings as buckets.
class Heap {
public:
    // A seed chosen at random, different each run, as in Lua: the hashes,
    // and so the order of `pairs`, change from run to run.
    static std::uint32_t random_seed();

    // `seed` decides every string hash; fix it to get the same hashes every
    // run (tests, debugging).
    explicit Heap(std::uint32_t seed = random_seed());

    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&) = delete;
    Heap& operator=(Heap&&) = delete;
    ~Heap();

    // A string holding `bytes`: the existing one for a short string made
    // before, a new one otherwise.
    const String* new_string(std::string_view bytes);

    [[nodiscard]] std::uint32_t seed() const { return seed_; }

    // How many strings the Heap owns, and how many are interned.
    [[nodiscard]] std::size_t string_count() const { return string_count_; }

    [[nodiscard]] std::size_t short_string_count() const { return short_count_; }

private:
    String* allocate(std::string_view bytes, std::uint32_t hash, bool is_short);
    void grow_buckets();

    std::uint32_t seed_;
    String* objects_ = nullptr;    // every string, newest first
    std::vector<String*> buckets_; // short strings by hash; a power of two long
    std::size_t string_count_ = 0;
    std::size_t short_count_ = 0;
};

inline Value Value::string(const String* s) {
    return Value{s->is_short() ? Tag::ShortString : Tag::LongString,
                 std::bit_cast<std::uint64_t>(s)};
}

// #s (V9.3, V9.4): a string's length in bytes. Tables join in stage 6; any
// other value is `attempt to get length of a X value`.
std::expected<Value, RuntimeError> length(Value value);

} // namespace lua2
