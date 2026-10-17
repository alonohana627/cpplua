#pragma once

#include <bit>
#include <cstdint>
#include <span>

#include "gc/object.hpp"
#include "value/string.hpp"
#include "value/value.hpp"

namespace cpplua {

class Heap;

// A Lua table (docs/spec/tables.md): a port of Lua 5.4's ltable.c.
//
// A table has two parts. The *array part* holds the values of the integer
// keys 1..n, for an n chosen so that more than half of its slots are in use;
// like the stack, it keeps tags and payloads in two arrays (one allocation,
// payloads first). The *hash part* holds every other key in a power-of-two
// array of nodes: a chained scatter table with Brent's variation, where a
// key that is not in its main position (the node its hash picks) means the
// node in that position holds a key that is. When a new key finds no free
// node, the table is *rehashed*: both parts are resized to fit the keys it
// has (rehash, computesizes).
//
// `alimit` is Lua's: usually the size of the array part, but `#` may lower
// it to the border it found, as a hint for the next `#` (luaH_getn); then
// the real size is the next power of two (real_array_size()).
//
// All memory comes from the Heap, so it counts toward the collector's
// pacing. Raw operations only: metamethods are the VM's business.
class Table : public GcObject {
public:
    // One entry of the hash part (Lua's Node): the value, the key, and the
    // offset to the next node of the key's chain (0 at the end).
    struct Node {
        std::uint64_t value = 0;
        std::uint64_t key = 0;
        Tag value_tag = Tag::Nil;
        Tag key_tag = Tag::Nil;
        // Lua's dead keys: the entry's value is nil and the collector may
        // free the key's object, but the node keeps its place in the chain
        // and `next` can still find it (by address).
        bool key_dead = false;
        std::int32_t next = 0;

        [[nodiscard]] Value key_value() const { return Value::from_parts(key_tag, key); }

        [[nodiscard]] bool key_is_free() const { return key_tag == Tag::Nil && !key_dead; }
    };

    // Where t[k]'s value lives: in the array part or in a node. Lua's
    // `const TValue *`, with absent keys (Lua's absentkey) as an empty slot.
    // A present slot may hold nil (an array slot, or a node whose value was
    // removed): storing there needs no new key.
    struct Slot {
        Tag* tag = nullptr;
        std::uint64_t* payload = nullptr;

        [[nodiscard]] bool absent() const { return tag == nullptr; }

        // Lua's isempty: absent, or present with nil.
        [[nodiscard]] bool empty() const { return tag == nullptr || *tag == Tag::Nil; }

        [[nodiscard]] Value value() const {
            return tag == nullptr ? Value::nil() : Value::from_parts(*tag, *payload);
        }

        void store(Value v) const {
            *tag = v.tag();
            *payload = v.payload();
        }
    };

    // What next() found.
    enum class NextResult : std::uint8_t { Found, End, InvalidKey };

    Table();
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) = delete;
    Table& operator=(Table&&) = delete;
    ~Table() = default;

    // ---- Reading (luaH_get*) ----

    // t[key] for an integer key (luaH_getint).
    [[nodiscard]] Slot get_int(std::int64_t key) {
        if (static_cast<std::uint64_t>(key) - 1U < own_word) [[likely]] {
            const auto i = static_cast<std::uint32_t>(key - 1);
            return {.tag = array_tags_ + i, .payload = array_values_ + i};
        }
        return get_int_slow(key);
    }

    // t[key] for a short (interned) string (luaH_getshortstr).
    [[nodiscard]] Slot get_short_string(const String* key) {
        Node* n = nodes_ + (key->hash() & node_mask());
        for (;;) {
            if (n->key_tag == Tag::ShortString && !n->key_dead &&
                n->key == std::bit_cast<std::uint64_t>(key)) {
                return {.tag = &n->value_tag, .payload = &n->value};
            }
            if (n->next == 0) {
                return {};
            }
            n += n->next;
        }
    }

    // t[key] for any string (luaH_getstr).
    [[nodiscard]] Slot get_string(const String* key);

    // t[key] for any value (luaH_get): nil is never present; a float with an
    // integral value is looked up as that integer.
    [[nodiscard]] Slot get(Value key);

    // ---- Writing (luaH_set*) ----
    //
    // The key must be valid: not nil, not NaN (check_key). Every write goes
    // through the backward barrier for the key and the value.

    // The error a key would give if stored: "table index is nil" or "table
    // index is NaN", or nullptr.
    [[nodiscard]] static const char* check_key(Value key) {
        if (key.is_nil()) [[unlikely]] {
            return "table index is nil";
        }
        if (key.is_float() && key.as_float() != key.as_float()) [[unlikely]] {
            return "table index is NaN";
        }
        return nullptr;
    }

    // Stores into the slot that get() returned for `key` (luaH_finishset):
    // a new key if it was absent.
    void finish_set(Heap& heap, Value key, Slot slot, Value value);

    // t[key] = value (luaH_set).
    void set(Heap& heap, Value key, Value value);

    // t[key] = value for an integer key (luaH_setint).
    void set_int(Heap& heap, std::int64_t key, Value value);

    // ---- Sizes ----

    // Gives the table exactly these sizes, moving every entry (luaH_resize).
    void resize(Heap& heap, std::uint32_t array_size, std::uint32_t hash_size);

    // A new array size, the hash part kept (luaH_resizearray).
    void resize_array(Heap& heap, std::uint32_t array_size);

    // #t without metamethods (luaH_getn): a border (docs/spec/tables.md T3).
    [[nodiscard]] std::uint64_t length();

    // ---- Traversal ----

    // The entry after `key` (nil for the first) into key and value
    // (luaH_next).
    NextResult next_entry(Value& key, Value& value);

    // ---- For the collector and the VM ----

    // The real size of the array part (luaH_realasize).
    [[nodiscard]] std::uint32_t real_array_size() const;

    // The array part for direct access: alimit slots are always valid
    // (SETLIST writes there after resizing).
    [[nodiscard]] std::uint32_t array_limit() const { return own_word; }

    [[nodiscard]] Tag* array_tags() { return array_tags_; }

    [[nodiscard]] std::uint64_t* array_values() { return array_values_; }

    // The hash part; empty when there is none (the shared dummy node is not
    // part of it).
    [[nodiscard]] std::span<Node> nodes() {
        return has_hash_part() ? std::span<Node>{nodes_, node_count()} : std::span<Node>{};
    }

    [[nodiscard]] bool has_hash_part() const { return last_free_ != nullptr; }

    [[nodiscard]] std::uint32_t node_count() const { return 1U << own_byte; }

    // Lua's clearkey: a node whose value is nil lets its key die.
    static void clear_key(Node& n) {
        if (n.key_tag >= first_collectable_tag) {
            n.key_dead = true;
        }
    }

    // Gives all memory back; the Heap calls it before freeing the table.
    void release(Heap& heap);

    // ---- The metamethod cache (ltm.h's fasttm) ----
    //
    // Lua's flags: bit e set means this table, as a metatable, is known to
    // lack metamethod e (for the events up to __eq). Cleared whenever the
    // table changes in a way that may add one.

    [[nodiscard]] bool knows_absent(unsigned event_bit) const {
        return (own_flags & event_bit) != 0;
    }

    void set_absent(unsigned event_bit) {
        own_flags = static_cast<std::uint8_t>(own_flags | event_bit);
    }

    void invalidate_metamethod_cache() {
        own_flags = static_cast<std::uint8_t>(own_flags & not_real_size_bit);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): the VM's fast path
    Table* metatable = nullptr;
    GcObject* gc_list = nullptr; // the collector's gray lists
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    [[nodiscard]] std::uint32_t node_mask() const { return node_count() - 1; }

    [[nodiscard]] Slot get_int_slow(std::int64_t key);
    [[nodiscard]] Slot get_generic(Value key, bool dead_ok);
    [[nodiscard]] Node* main_position(Value key);
    [[nodiscard]] Node* free_position();
    void new_key(Heap& heap, Value key, Value value);
    void rehash(Heap& heap, Value extra_key);
    void set_node_vector(Heap& heap, std::uint32_t size);
    void free_nodes(Heap& heap);
    [[nodiscard]] std::uint32_t array_index_of(Value key);
    [[nodiscard]] std::uint32_t count_array(std::span<std::uint32_t> nums) const;
    [[nodiscard]] std::uint32_t count_hash(std::span<std::uint32_t> nums,
                                           std::uint32_t& array_keys) const;
    [[nodiscard]] std::uint64_t hash_search(std::uint64_t j);
    [[nodiscard]] bool limit_equals_size() const;
    std::uint32_t set_limit_to_size();
    void set_array(Heap& heap, std::uint32_t old_size, std::uint32_t new_size);

    // In the header (GcObject's own fields): alimit is Lua's alimit,
    // own_byte its lsizenode (log2 of the node count), and own_flags the
    // metamethod cache plus BITRAS (alimit is not the real array size).
    static constexpr std::uint8_t not_real_size_bit = 0x80;

    [[nodiscard]] bool limit_is_size() const { return (own_flags & not_real_size_bit) == 0; }

    void set_limit_is_size(bool is_size) {
        own_flags = static_cast<std::uint8_t>(is_size ? cleared(own_flags, not_real_size_bit)
                                                      : own_flags | not_real_size_bit);
    }

    std::uint64_t* array_values_ = nullptr;
    Tag* array_tags_ = nullptr; // right after the payloads, in the same block
    Node* nodes_;
    Node* last_free_ = nullptr; // nullptr: the shared dummy node, no hash part
};

inline Value Value::table(const Table* t) {
    return Value{Tag::Table, std::bit_cast<std::uint64_t>(t)};
}

} // namespace cpplua
