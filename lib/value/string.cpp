#include "value/string.hpp"

#include <array>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "value/convert.hpp"

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
        String* s = allocate(bytes.size(), seed_, false);
        std::memcpy(bytes_of(s), bytes.data(), bytes.size());
        return s;
    }
    return intern(bytes);
}

const String* Heap::new_string(std::span<const std::string_view> parts) {
    std::size_t length = 0;
    for (const std::string_view part : parts) {
        length += part.size();
    }
    if (length <= max_short_length) {
        // Put together on the stack, then interned (luaV_concat's buff).
        std::array<char, max_short_length> buffer{};
        std::size_t at = 0;
        for (const std::string_view part : parts) {
            part.copy(buffer.data() + at, part.size());
            at += part.size();
        }
        return intern({buffer.data(), length});
    }
    // Written straight into the new long string (luaS_createlngstrobj).
    String* s = allocate(length, seed_, false);
    char* out = bytes_of(s);
    for (const std::string_view part : parts) {
        std::memcpy(out, part.data(), part.size());
        out += part.size();
    }
    return s;
}

const String* Heap::intern(std::string_view bytes) {
    // A short string: the one already made, if any (Lua's internshrstr).
    const std::uint32_t hash = string_hash(bytes, seed_);
    String*& bucket = buckets_[hash & (buckets_.size() - 1)];
    for (const String* s = bucket; s != nullptr; s = s->next_in_bucket_) {
        if (s->view() == bytes) {
            return s;
        }
    }

    String* s = allocate(bytes.size(), hash, true);
    std::memcpy(bytes_of(s), bytes.data(), bytes.size());
    s->next_in_bucket_ = bucket;
    bucket = s;
    ++short_count_;
    if (short_count_ >= buckets_.size()) {
        grow_buckets();
    }
    return s;
}

String* Heap::allocate(std::size_t length, std::uint32_t hash, bool is_short) {
    // One block: the header, the bytes, and a '\0'.
    void* memory = ::operator new(sizeof(String) + length + 1);
    // The Heap owns every string through objects_, and frees it in ~Heap.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* s = new (memory) String(length, hash, is_short);
    bytes_of(s)[length] = '\0';

    s->next_object_ = objects_;
    objects_ = s;
    ++string_count_;
    return s;
}

char* Heap::bytes_of(String* s) {
    // Where String::data() reads them: right after the header.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<char*>(s + 1);
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

namespace {

// The pieces of a concatenation: each string's bytes, or the text of each
// number, written into `numbers`. Both spans have room for every value.
std::span<const std::string_view> join_parts(std::span<const Value> values,
                                             std::span<NumberText> numbers,
                                             std::span<std::string_view> parts) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].is_string()) {
            parts[i] = values[i].as_string()->view();
        } else {
            numbers[i] = number_text(values[i]).value_or(NumberText{});
            parts[i] = numbers[i].view();
        }
    }
    return parts.first(values.size());
}

} // namespace

std::expected<Value, RuntimeError> concatenate(Heap& heap, std::span<const Value> values) {
    if (values.size() == 1) {
        return values.front();
    }
    const auto joinable = [](Value v) { return v.is_string() || v.is_number(); };

    // Lua's luaV_concat joins from the right and stops at the first pair it
    // cannot join, so the error is about the rightmost operand that is
    // neither a string nor a number, except that the last pair names its
    // left operand when both are wrong (luaG_concaterror).
    for (std::size_t k = values.size(); k-- > 0;) {
        if (joinable(values[k])) {
            continue;
        }
        const Value culprit = (k == values.size() - 1 && k > 0 && !joinable(values[k - 1]))
                                  ? values[k - 1]
                                  : values[k];
        std::string message = "attempt to concatenate a ";
        message += type_name(culprit.type());
        message += " value";
        return std::unexpected(RuntimeError{std::move(message)});
    }

    // Numbers become text (V6). A typical chain is short, so its pieces fit
    // in arrays on the stack; only a long chain allocates for them.
    constexpr std::size_t on_stack = 8;
    if (values.size() <= on_stack) {
        std::array<NumberText, on_stack> numbers;
        std::array<std::string_view, on_stack> parts;
        return Value::string(heap.new_string(join_parts(values, numbers, parts)));
    }
    std::vector<NumberText> numbers(values.size());
    std::vector<std::string_view> parts(values.size());
    return Value::string(heap.new_string(join_parts(values, numbers, parts)));
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
