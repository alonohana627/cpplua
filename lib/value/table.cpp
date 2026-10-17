#include "value/table.hpp"

#include <memory>

#include "gc/heap.hpp"

namespace lua2 {

namespace {

// Short strings are interned, so the same pointer; long strings compare
// their bytes (a short and a long string are never equal).
bool same_key(const String* a, const String* b) {
    return a == b || (!a->is_short() && !b->is_short() && a->view() == b->view());
}

} // namespace

const Table::Entry* Table::find(const String* key) const {
    if (capacity_ == 0) {
        return nullptr;
    }
    const std::uint32_t mask = capacity_ - 1;
    for (std::uint32_t i = key->hash() & mask;; i = (i + 1) & mask) {
        const Entry& entry = entries_[i];
        if (entry.key == nullptr) {
            return nullptr;
        }
        if (same_key(entry.key, key)) {
            return &entry;
        }
    }
}

Value Table::get(const String* key) const {
    const Entry* entry = find(key);
    return entry != nullptr ? Value::from_parts(entry->tag, entry->payload) : Value::nil();
}

void Table::set(Heap& heap, const String* key, Value value) {
    // find() is const; the entry it finds is ours to change.
    auto* entry = const_cast<Entry*>(find(key)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    if (entry == nullptr) {
        if (value.is_nil()) {
            return; // nothing to remove
        }
        if ((count_ + 1) * 4 > capacity_ * 3) {
            grow(heap);
        }
        const std::uint32_t mask = capacity_ - 1;
        std::uint32_t i = key->hash() & mask;
        while (entries_[i].key != nullptr) {
            i = (i + 1) & mask;
        }
        entry = &entries_[i];
        entry->key = key;
        ++count_;
        heap.barrier_back(this, Value::string(key));
    }
    entry->tag = value.tag();
    entry->payload = value.payload();
    heap.barrier_back(this, value);
}

void Table::grow(Heap& heap) {
    // Twice the slots, the live entries moved over; entries holding nil are
    // dropped here.
    const std::uint32_t capacity = capacity_ == 0 ? 4 : capacity_ * 2;
    auto* fresh = static_cast<Entry*>(heap.allocate(capacity * sizeof(Entry)));
    std::uninitialized_value_construct_n(fresh, capacity);
    const std::uint32_t mask = capacity - 1;
    std::uint32_t count = 0;
    for (const Entry& old : entries()) {
        if (old.key == nullptr || old.tag == Tag::Nil) {
            continue;
        }
        std::uint32_t i = old.key->hash() & mask;
        while (fresh[i].key != nullptr) {
            i = (i + 1) & mask;
        }
        fresh[i] = old;
        ++count;
    }
    release(heap);
    entries_ = fresh;
    capacity_ = capacity;
    count_ = count;
}

void Table::release(Heap& heap) {
    if (entries_ != nullptr) {
        heap.free(entries_, capacity_ * sizeof(Entry));
        entries_ = nullptr;
        capacity_ = 0;
        count_ = 0;
    }
}

} // namespace lua2
