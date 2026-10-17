#include "value/string.hpp"

#include <cstring>
#include <new>
#include <random>
#include <string>

namespace lua2 {

namespace {

// Lua's MINSTRTABSIZE: the string table starts with 128 buckets.
constexpr std::size_t first_bucket_count = 128;

} // namespace

std::uint32_t Heap::random_seed() {
    std::random_device device;
    return device();
}

Heap::Heap(std::uint32_t seed) : seed_(seed), buckets_(first_bucket_count, nullptr) {}

Heap::~Heap() {
    String* s = objects_;
    while (s != nullptr) {
        String* next = s->next_object_;
        s->~String();
        ::operator delete(static_cast<void*>(s));
        s = next;
    }
}

const String* Heap::new_string(std::string_view bytes) {
    if (bytes.size() > max_short_length) {
        // Long strings are not interned, and their hash waits until it is
        // needed: until then the string keeps the seed in its place.
        return allocate(bytes, seed_, false);
    }

    // A short string: the one already made, if any (Lua's internshrstr).
    const std::uint32_t hash = string_hash(bytes, seed_);
    String*& bucket = buckets_[hash & (buckets_.size() - 1)];
    for (const String* s = bucket; s != nullptr; s = s->next_in_bucket_) {
        if (s->view() == bytes) {
            return s;
        }
    }

    String* s = allocate(bytes, hash, true);
    s->next_in_bucket_ = bucket;
    bucket = s;
    ++short_count_;
    if (short_count_ >= buckets_.size()) {
        grow_buckets();
    }
    return s;
}

String* Heap::allocate(std::string_view bytes, std::uint32_t hash, bool is_short) {
    // One block: the header, the bytes, and a '\0'.
    void* memory = ::operator new(sizeof(String) + bytes.size() + 1);
    // The Heap owns every string through objects_, and frees it in ~Heap.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* s = new (memory) String(bytes.size(), hash, is_short);
    auto* data = static_cast<char*>(memory) + sizeof(String);
    std::memcpy(data, bytes.data(), bytes.size());
    data[bytes.size()] = '\0';

    s->next_object_ = objects_;
    objects_ = s;
    ++string_count_;
    return s;
}

void Heap::grow_buckets() {
    // Twice the buckets, and every short string moved to its new bucket
    // (Lua's luaS_resize). The bucket count stays a power of two, so a
    // bucket is found with a mask instead of a division.
    std::vector<String*> bigger(buckets_.size() * 2, nullptr);
    for (String* chain : buckets_) {
        while (chain != nullptr) {
            String* next = chain->next_in_bucket_;
            String*& bucket = bigger[chain->hash_ & (bigger.size() - 1)];
            chain->next_in_bucket_ = bucket;
            bucket = chain;
            chain = next;
        }
    }
    buckets_ = std::move(bigger);
}

std::expected<Value, RuntimeError> length(Value value) {
    if (value.is_string()) {
        return Value::integer(static_cast<std::int64_t>(value.as_string()->length()));
    }
    std::string message = "attempt to get length of a ";
    message += type_name(value.type());
    message += " value";
    return std::unexpected(RuntimeError{std::move(message)});
}

} // namespace lua2
