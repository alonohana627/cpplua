// Tables: a port of Lua 5.4.9's ltable.c (docs/final_docs/tables.md). The
// functions keep Lua's names in their comments, and its structure, so the two
// can be read side by side.

#include "value/table.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <span>

#include "gc/heap.hpp"
#include "value/number.hpp"

namespace lua2 {

namespace {

// MAXABITS: the largest n such that 2^n array slots fit an unsigned int;
// MAXASIZE, the largest array part; MAXHBITS, the same for the hash part.
constexpr int max_array_bits = static_cast<int>((sizeof(int) * CHAR_BIT) - 1);
constexpr std::uint32_t max_array_size = 1U << static_cast<unsigned>(max_array_bits);
constexpr int max_hash_bits = max_array_bits - 1;

// The node every table without a hash part points to (Lua's dummynode): a
// free node with no next. Nothing ever writes to it.
const Table::Node dummy_node{};

Table::Node* dummy() {
    return const_cast<Table::Node*>(&dummy_node); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

// Lua's ispow2, which counts 0 as a power of two.
constexpr bool is_power_of_2(std::uint32_t x) {
    return (x & (x - 1)) == 0;
}

// luaO_ceillog2: ceil(log2(x)), for x >= 1.
int ceil_log2(std::uint32_t x) {
    return x <= 1 ? 0 : std::bit_width(x - 1);
}

// arrayindex: the index for `k` if it may live in the array part, 0
// otherwise.
std::uint32_t array_index(std::int64_t k) {
    if (static_cast<std::uint64_t>(k) - 1U < max_array_size) {
        return static_cast<std::uint32_t>(k);
    }
    return 0;
}

// l_hashfloat: frexp's mantissa scaled to an int, plus the exponent.
int hash_float(double n) {
    int exponent = 0;
    n = std::frexp(n, &exponent) * -static_cast<double>(INT_MIN);
    // lua_numbertointeger: in range, then truncated as C casts do.
    constexpr double two_to_63 = 9223372036854775808.0;
    if (n < -two_to_63 || n >= two_to_63 || n != n) { // inf, -inf or NaN
        return 0;
    }
    const auto ni = static_cast<std::int64_t>(n);
    const unsigned u = static_cast<unsigned>(exponent) + static_cast<unsigned>(ni);
    return static_cast<int>(u <= static_cast<unsigned>(INT_MAX) ? u : ~u);
}

// point2uint: the low bits of an address.
unsigned pointer_hash(std::uint64_t p) {
    return static_cast<unsigned>(p & UINT_MAX);
}

// Long strings compare their bytes (luaS_eqlngstr).
bool same_long_string(std::uint64_t a, std::uint64_t b) {
    if (a == b) {
        return true;
    }
    const auto* x = std::bit_cast<const String*>(a);
    const auto* y = std::bit_cast<const String*>(b);
    return x->length() == y->length() && std::memcmp(x->data(), y->data(), x->length()) == 0;
}

// equalkey: whether `key` is the key of node `n` (raw equality). With
// `dead_ok`, a dead key matches the object it held, by address.
bool equal_key(Value key, const Table::Node& n, bool dead_ok) {
    if (n.key_dead) {
        return dead_ok && key.is_collectable() && key.payload() == n.key;
    }
    if (key.tag() != n.key_tag) {
        return false;
    }
    switch (n.key_tag) {
    case Tag::Nil:
    case Tag::False:
    case Tag::True: return true;
    case Tag::Float: return std::bit_cast<double>(key.payload()) == std::bit_cast<double>(n.key);
    case Tag::LongString: return same_long_string(key.payload(), n.key);
    default: return key.payload() == n.key; // integers, functions, objects, short strings
    }
}

} // namespace

Table::Table() : nodes_(dummy()) {
    type = ObjectType::Table;
}

// ---- Reading ----

std::uint32_t Table::real_array_size() const {
    // luaH_realasize: alimit, or the smallest power of two above it.
    if (limit_equals_size()) {
        return own_word;
    }
    return std::bit_ceil(own_word);
}

bool Table::limit_equals_size() const {
    // limitequalsasize
    return limit_is_size() || is_power_of_2(own_word);
}

std::uint32_t Table::set_limit_to_size() {
    // setlimittosize
    own_word = real_array_size();
    set_limit_is_size(true);
    return own_word;
}

Table::Slot Table::get_int_slow(std::int64_t key) {
    // The rest of luaH_getint. A key past alimit may still be in the array
    // part when alimit is a hint below its real size: the "Xmilia trick"
    // checks key - 1 against that size, a power of two.
    const std::uint64_t index = static_cast<std::uint64_t>(key) - 1U;
    if (!limit_is_size() && (index & ~(std::uint64_t{own_word} - 1U)) < own_word) {
        own_word = static_cast<std::uint32_t>(key); // probably '#t' is here now
        return {.tag = array_tags_ + index, .payload = array_values_ + index};
    }
    const std::uint64_t size = node_count();
    Node* n = nodes_ + (static_cast<std::uint64_t>(key) % ((size - 1) | 1U)); // hashint
    for (;;) {
        if (n->key_tag == Tag::Integer && !n->key_dead &&
            std::bit_cast<std::int64_t>(n->key) == key) {
            return {.tag = &n->value_tag, .payload = &n->value};
        }
        if (n->next == 0) {
            return {};
        }
        n += n->next;
    }
}

Table::Slot Table::get_string(const String* key) {
    if (key->is_short()) {
        return get_short_string(key);
    }
    return get_generic(Value::string(key), false);
}

Table::Slot Table::get(Value key) {
    switch (key.tag()) {
    case Tag::ShortString: return get_short_string(key.as_string());
    case Tag::Integer: return get_int(key.as_integer());
    case Tag::Nil: return {};
    case Tag::Float:
        if (const auto k = float_to_int(key.as_float())) { // an integral float
            return get_int(*k);
        }
        return get_generic(key, false);
    default: return get_generic(key, false);
    }
}

Table::Slot Table::get_generic(Value key, bool dead_ok) {
    // getgeneric: not for integers, which may be in the array part, nor
    // for floats with integral values.
    Node* n = main_position(key);
    for (;;) {
        if (equal_key(key, *n, dead_ok)) {
            return {.tag = &n->value_tag, .payload = &n->value};
        }
        if (n->next == 0) {
            return {};
        }
        n += n->next;
    }
}

Table::Node* Table::main_position(Value key) {
    // mainpositionTV
    const std::uint64_t size = node_count();
    const std::uint64_t odd = (size - 1) | 1U; // hashmod: avoid powers of two
    switch (key.tag()) {
    case Tag::Integer: return nodes_ + (key.payload() % odd);
    case Tag::Float: return nodes_ + (static_cast<std::uint64_t>(hash_float(key.as_float())) % odd);
    case Tag::ShortString:
    case Tag::LongString: return nodes_ + (key.as_string()->hash() & (size - 1));
    case Tag::False: return nodes_;
    case Tag::True: return nodes_ + (1U & (size - 1));
    default: return nodes_ + (pointer_hash(key.payload()) % odd); // functions, objects
    }
}

// ---- Writing ----

Table::Node* Table::free_position() {
    // getfreepos: the free nodes are searched from the end down.
    if (last_free_ != nullptr) {
        while (last_free_ > nodes_) {
            --last_free_;
            if (last_free_->key_is_free()) {
                return last_free_;
            }
        }
    }
    return nullptr;
}

void Table::new_key(Heap& heap, Value key, Value value) {
    // luaH_newkey: a key the table does not have. If its main position is
    // taken, either the node there is out of its own main position (move it
    // to a free node, and take its place) or it is in it (go to a free node,
    // chained from there).
    if (key.is_float()) {
        if (const auto k = float_to_int(key.as_float())) {
            key = Value::integer(*k); // stored as an integer (T1.3)
        }
    }
    if (value.is_nil()) {
        return; // nil values are not stored
    }
    Node* mp = main_position(key);
    if (mp->value_tag != Tag::Nil || !has_hash_part()) { // main position taken?
        Node* f = free_position();
        if (f == nullptr) { // no free node: grow, then insert
            rehash(heap, key);
            set(heap, key, value);
            return;
        }
        // (A taken node has a value, so its key is alive.)
        Node* other = main_position(mp->key_value());
        if (other != mp) {
            // The colliding node is out of its main position: move it to the
            // free node, and take its place.
            while (other + other->next != mp) { // find the previous node
                other += other->next;
            }
            other->next = static_cast<std::int32_t>(f - other); // rechain to the free node
            *f = *mp;                                           // (mp's next goes along)
            if (mp->next != 0) {
                f->next += static_cast<std::int32_t>(mp - f);
                mp->next = 0;
            }
            mp->value_tag = Tag::Nil;
        } else {
            // The colliding node is in its own main position: the new key
            // goes to the free node.
            if (mp->next != 0) {
                f->next = static_cast<std::int32_t>((mp + mp->next) - f);
            }
            mp->next = static_cast<std::int32_t>(f - mp);
            mp = f;
        }
    }
    mp->key = key.payload();
    mp->key_tag = key.tag();
    mp->key_dead = false;
    heap.barrier_back(this, key);
    mp->value = value.payload();
    mp->value_tag = value.tag();
    heap.barrier_back(this, value);
}

void Table::finish_set(Heap& heap, Value key, Slot slot, Value value) {
    // luaH_finishset
    invalidate_metamethod_cache(); // it may be a metatable
    if (slot.absent()) {
        new_key(heap, key, value);
        return;
    }
    slot.store(value);
    heap.barrier_back(this, value);
}

void Table::set(Heap& heap, Value key, Value value) {
    finish_set(heap, key, get(key), value);
}

void Table::set_int(Heap& heap, std::int64_t key, Value value) {
    // luaH_setint
    const Slot slot = get_int(key);
    if (slot.absent()) {
        new_key(heap, Value::integer(key), value);
        invalidate_metamethod_cache();
        return;
    }
    slot.store(value);
    heap.barrier_back(this, value);
}

// ---- Rehash ----

std::uint32_t Table::count_array(std::span<std::uint32_t> nums) const {
    // numusearray: nums[i] = the keys k with 2^(i-1) < k <= 2^i in the
    // array part; returns how many there are.
    std::uint32_t used = 0;
    std::uint32_t i = 1;
    const std::uint32_t size = own_word; // set_limit_to_size was called
    std::uint64_t two_to_lg = 1;
    for (int lg = 0; lg <= max_array_bits; ++lg, two_to_lg *= 2) {
        std::uint32_t count = 0;
        auto limit = static_cast<std::uint32_t>(std::min<std::uint64_t>(two_to_lg, size));
        if (two_to_lg > size && i > limit) {
            break;
        }
        for (; i <= limit; ++i) {
            if (array_tags_[i - 1] != Tag::Nil) {
                ++count;
            }
        }
        nums[static_cast<std::size_t>(lg)] += count;
        used += count;
    }
    return used;
}

std::uint32_t Table::count_hash(std::span<std::uint32_t> nums, std::uint32_t& array_keys) const {
    // numusehash
    std::uint32_t total = 0;
    std::uint32_t integers = 0;
    if (last_free_ != nullptr) {
        for (std::uint32_t i = node_count(); i-- > 0;) {
            const Node& n = nodes_[i];
            if (n.value_tag != Tag::Nil) {
                if (n.key_tag == Tag::Integer) {
                    if (const std::uint32_t k = array_index(std::bit_cast<std::int64_t>(n.key))) {
                        ++nums[static_cast<std::size_t>(ceil_log2(k))];
                        ++integers;
                    }
                }
                ++total;
            }
        }
    }
    array_keys += integers;
    return total;
}

void Table::set_node_vector(Heap& heap, std::uint32_t size) {
    // setnodevector: a fresh hash part of at least `size` nodes, or none.
    if (size == 0) {
        nodes_ = dummy();
        own_byte = 0;
        last_free_ = nullptr;
        return;
    }
    const int lsize = ceil_log2(size);
    if (lsize > max_hash_bits) {
        std::abort(); // "table overflow": more memory than exists
    }
    const std::uint32_t count = 1U << static_cast<unsigned>(lsize);
    nodes_ = static_cast<Node*>(heap.allocate(count * sizeof(Node)));
    std::uninitialized_value_construct_n(nodes_, count);
    own_byte = static_cast<std::uint8_t>(lsize);
    last_free_ = nodes_ + count; // every node is free
}

void Table::free_nodes(Heap& heap) {
    // freehash
    if (has_hash_part()) {
        heap.free(nodes_, node_count() * sizeof(Node));
    }
}

void Table::set_array(Heap& heap, std::uint32_t old_size, std::uint32_t new_size) {
    // luaM_reallocvector for the array part: the first slots keep their
    // values, new slots are nil.
    std::uint64_t* values = nullptr;
    Tag* tags = nullptr;
    if (new_size > 0) {
        void* block = heap.allocate(new_size * (sizeof(std::uint64_t) + sizeof(Tag)));
        values = static_cast<std::uint64_t*>(block);
        tags = reinterpret_cast<Tag*>(values + new_size); // NOLINT(*-reinterpret-cast)
        const std::uint32_t kept = std::min(old_size, new_size);
        std::memcpy(values, array_values_, kept * sizeof(std::uint64_t));
        std::memcpy(tags, array_tags_, kept * sizeof(Tag));
        std::fill(tags + kept, tags + new_size, Tag::Nil);
        std::fill(values + kept, values + new_size, 0);
    }
    if (old_size > 0) {
        heap.free(array_values_, old_size * (sizeof(std::uint64_t) + sizeof(Tag)));
    }
    array_values_ = values;
    array_tags_ = tags;
}

void Table::resize(Heap& heap, std::uint32_t array_size, std::uint32_t hash_size) {
    // luaH_resize: build the new hash part aside; move the entries of a
    // shrinking array into it; resize the array; then put the old hash
    // part's entries into the new parts.
    const std::uint32_t old_array_size = set_limit_to_size();
    Node* const old_nodes = nodes_;
    const Node* const old_last_free = last_free_;
    const std::uint8_t old_log = own_byte;
    set_node_vector(heap, hash_size); // the new hash part, now the table's
    if (array_size < old_array_size) {
        // Pretend the array has its new size, and move the vanishing slice
        // into the new hash part.
        own_word = array_size;
        for (std::uint32_t i = array_size; i < old_array_size; ++i) {
            if (array_tags_[i] != Tag::Nil) {
                set_int(heap, std::int64_t{i} + 1,
                        Value::from_parts(array_tags_[i], array_values_[i]));
            }
        }
    }
    set_array(heap, old_array_size, array_size);
    own_word = array_size;
    set_limit_is_size(true);
    // Re-insert the old hash part (reinsert).
    if (old_last_free != nullptr) {
        const std::uint32_t old_count = 1U << old_log;
        for (std::uint32_t j = 0; j < old_count; ++j) {
            const Node& old = old_nodes[j];
            if (old.value_tag != Tag::Nil) {
                set(heap, old.key_value(), Value::from_parts(old.value_tag, old.value));
            }
        }
        heap.free(old_nodes, old_count * sizeof(Node));
    }
}

void Table::resize_array(Heap& heap, std::uint32_t array_size) {
    resize(heap, array_size, has_hash_part() ? node_count() : 0);
}

void Table::rehash(Heap& heap, Value extra_key) {
    // rehash: count the integer keys by powers of two, choose the largest
    // array size that is more than half full (computesizes), and give
    // everything else to the hash part.
    std::array<std::uint32_t, max_array_bits + 1> nums{};
    set_limit_to_size();
    std::uint32_t array_keys = count_array(nums); // keys in the array part
    std::uint32_t total = array_keys;
    total += count_hash(nums, array_keys);
    if (extra_key.is_integer()) {
        if (const std::uint32_t k = array_index(extra_key.as_integer())) {
            ++nums[static_cast<std::size_t>(ceil_log2(k))];
            ++array_keys;
        }
    }
    ++total;
    // computesizes
    std::uint32_t a = 0;
    std::uint32_t na = 0;
    std::uint32_t optimal = 0;
    std::uint32_t two_to_i = 1;
    for (std::size_t i = 0; two_to_i > 0 && array_keys > two_to_i / 2; ++i, two_to_i *= 2) {
        a += nums[i];
        if (a > two_to_i / 2) {
            optimal = two_to_i;
            na = a;
        }
    }
    resize(heap, optimal, total - na);
}

// ---- Length ----

std::uint64_t Table::hash_search(std::uint64_t j) {
    // hash_search: j is zero or present and j + 1 is present; double j
    // until absent, then a binary search.
    constexpr auto max_integer =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::uint64_t i = 0;
    if (j == 0) {
        ++j;
    }
    do {
        i = j;
        if (j <= max_integer / 2) {
            j *= 2;
        } else {
            j = max_integer;
            if (get_int(static_cast<std::int64_t>(j)).empty()) {
                break;
            }
            return j; // max integer is a border
        }
    } while (!get_int(static_cast<std::int64_t>(j)).empty());
    while (j - i > 1U) {
        const std::uint64_t m = (i + j) / 2;
        if (get_int(static_cast<std::int64_t>(m)).empty()) {
            j = m;
        } else {
            i = m;
        }
    }
    return i;
}

std::uint64_t Table::length() {
    // luaH_getn, case by case.
    const auto binary_search = [this](std::uint32_t i, std::uint32_t j) { // binsearch
        while (j - i > 1U) {
            const std::uint32_t m = (i + j) / 2;
            if (array_tags_[m - 1] == Tag::Nil) {
                j = m;
            } else {
                i = m;
            }
        }
        return i;
    };
    std::uint32_t limit = own_word;
    if (limit > 0 && array_tags_[limit - 1] == Tag::Nil) { // (1) a border before limit
        const bool pow2_real_size = !limit_is_size() || is_power_of_2(own_word); // ispow2realasize
        if (limit >= 2 && array_tags_[limit - 2] != Tag::Nil) {
            if (pow2_real_size && !is_power_of_2(limit - 1)) {
                own_word = limit - 1;
                set_limit_is_size(false);
            }
            return limit - 1;
        }
        const std::uint32_t border = binary_search(0, limit);
        if (pow2_real_size && border > real_array_size() / 2) {
            own_word = border;
            set_limit_is_size(false);
        }
        return border;
    }
    if (!limit_equals_size()) { // (2) limit is present, and the array goes on
        if (array_tags_[limit] == Tag::Nil) {
            return limit;
        }
        limit = real_array_size();
        if (array_tags_[limit - 1] == Tag::Nil) {
            const std::uint32_t border = binary_search(own_word, limit);
            own_word = border;
            return border;
        }
    }
    // (3) limit is the last element and is zero or present: the hash part.
    if (!has_hash_part() || get_int(std::int64_t{limit} + 1).empty()) {
        return limit;
    }
    return hash_search(limit);
}

// ---- Traversal ----

std::uint32_t Table::array_index_of(Value key) {
    // findindex: 0 for nil (the start), i for t[i] in the array part,
    // array size + node number + 1 for a node; UINT32_MAX when absent.
    if (key.is_nil()) {
        return 0;
    }
    const std::uint32_t size = real_array_size();
    const std::uint32_t i = key.is_integer() ? array_index(key.as_integer()) : 0;
    if (i - 1U < size) {
        return i;
    }
    const Slot slot = get_generic(key, true);
    if (slot.absent()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    const auto* node = reinterpret_cast<const Node*>(slot.payload); // NOLINT(*-reinterpret-cast)
    return static_cast<std::uint32_t>(node - nodes_) + 1 + size;
}

Table::NextResult Table::next_entry(Value& key, Value& value) {
    // luaH_next
    const std::uint32_t size = real_array_size();
    std::uint32_t i = array_index_of(key);
    if (i == std::numeric_limits<std::uint32_t>::max()) {
        return NextResult::InvalidKey;
    }
    for (; i < size; ++i) {
        if (array_tags_[i] != Tag::Nil) {
            key = Value::integer(std::int64_t{i} + 1);
            value = Value::from_parts(array_tags_[i], array_values_[i]);
            return NextResult::Found;
        }
    }
    if (has_hash_part()) {
        for (i -= size; i < node_count(); ++i) {
            const Node& n = nodes_[i];
            if (n.value_tag != Tag::Nil) {
                key = n.key_value();
                value = Value::from_parts(n.value_tag, n.value);
                return NextResult::Found;
            }
        }
    }
    return NextResult::End;
}

void Table::release(Heap& heap) {
    // luaH_free
    free_nodes(heap);
    nodes_ = dummy();
    last_free_ = nullptr;
    own_byte = 0;
    const std::uint32_t size = real_array_size();
    if (size > 0) {
        heap.free(array_values_, size * (sizeof(std::uint64_t) + sizeof(Tag)));
    }
    array_values_ = nullptr;
    array_tags_ = nullptr;
    own_word = 0;
    set_limit_is_size(true);
}

} // namespace lua2
