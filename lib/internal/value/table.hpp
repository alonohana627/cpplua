#pragma once

#include <bit>
#include <cstdint>
#include <span>

#include "gc/object.hpp"
#include "value/string.hpp"
#include "value/value.hpp"

namespace lua2 {

class Heap;

// A Lua table, for now only what the globals need: keys are strings, values
// anything. It exists so that _ENV can be the globals table, and `print`
// and global variables work, before stage 6 builds real tables (an array
// part, keys of every type, `#`, `next`, constructors). Until then the only
// table is the globals table.
//
// A hash table with open addressing: entries in one array, a power of two
// long, probed linearly from the key's hash. Assigning nil keeps the entry
// with a nil value, so lookups keep working; the array grows when three
// quarters full. Its memory comes from the Heap.
class Table : public GcObject {
public:
    struct Entry {
        const String* key = nullptr; // nullptr: an empty slot
        Tag tag = Tag::Nil;
        std::uint64_t payload = 0;
    };

    Table() { type = ObjectType::Table; }

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) = delete;
    Table& operator=(Table&&) = delete;
    ~Table() = default;

    // t[key], nil when absent.
    [[nodiscard]] Value get(const String* key) const;

    // t[key] = value, with the collector's backward barrier (a black table
    // gets a white key or value).
    void set(Heap& heap, const String* key, Value value);

    // Every slot, for the collector.
    [[nodiscard]] std::span<const Entry> entries() const { return {entries_, capacity_}; }

    // Gives the entries' memory back; the Heap calls it before freeing the
    // table.
    void release(Heap& heap);

    GcObject* gc_list = nullptr; // NOLINT(*-non-private-member-variables-in-classes): collector's

private:
    [[nodiscard]] const Entry* find(const String* key) const;
    void grow(Heap& heap);

    Entry* entries_ = nullptr;
    std::uint32_t capacity_ = 0;
    std::uint32_t count_ = 0; // slots with a key
};

inline Value Value::table(const Table* t) {
    return Value{Tag::Table, std::bit_cast<std::uint64_t>(t)};
}

} // namespace lua2
