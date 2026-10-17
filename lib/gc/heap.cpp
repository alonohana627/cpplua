#include "gc/heap.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <new>
#include <random>
#include <utility>

#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/proto.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// Lua's MINSTRTABSIZE: the string table starts with 128 buckets.
constexpr std::size_t first_bucket_count = 128;

} // namespace

std::uint32_t Heap::random_seed() {
    std::random_device device;
    return device();
}

Heap::Heap(std::uint32_t seed)
    // As Lua's lua_newstate: the heap itself counts as allocated, the debt
    // starts at zero, so the first check after any allocation starts a
    // cycle.
    : seed_(seed), total_bytes_(sizeof(Heap)) {
    resize_string_table(first_bucket_count);
    // The main thread is the first object, at the end of the list of all
    // objects. It is never collected: it is the root everything else is
    // reached from. (It needs the string table and the pool, so it cannot be
    // made in the member initializers.)
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    main_thread_ = link_new<State>(allocate(sizeof(State)), ObjectType::Thread);
    main_thread_->non_yieldable = 1; // the main thread cannot yield
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): the same
    running_ = main_thread_;
    // The names of the metamethods, kept forever (luaT_init).
    for (std::size_t e = 0; e < event_count; ++e) {
        event_strings_.at(e) = new_string(event_names.at(e));
        fix(event_strings_.at(e));
    }
    name_string_ = new_string("__name");
    fix(name_string_);
    memory_error_string_ = new_string("not enough memory");
    fix(memory_error_string_);
    hook_key_string_ = new_string("_HOOKKEY");
    fix(hook_key_string_);
    constexpr std::array<std::string_view, 5> hook_events{"call", "return", "line", "count",
                                                          "tail call"};
    for (std::size_t e = 0; e < hook_events.size(); ++e) {
        hook_event_strings_.at(e) = new_string(hook_events.at(e));
        fix(hook_event_strings_.at(e));
    }
    // The registry's tables: the globals, and the loaded modules with the
    // globals as "_G" (luaL_openlibs does that through luaopen_base).
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): the same
    globals_ = new_table();
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): the same
    loaded_ = new_table();
    loaded_->set(*this, Value::string(new_string("_G")), Value::table(globals_));
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): the same
    references_ = new_table();
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): the same
    registry_ = new_table();
    registry_->set_int(*this, 1, Value::thread(main_thread_));
    registry_->set_int(*this, 2, Value::table(globals_));
    registry_->set(*this, Value::string(new_string("_LOADED")), Value::table(loaded_));
}

Heap::~Heap() {
    close();
    // Free everything (luaC_freeallobjects): the objects, then the main
    // thread, which is the last one in the list.
    GcObject* o = all_;
    while (o != main_thread_) {
        GcObject* next = o->next;
        free_object(o);
        o = next;
    }
    main_thread_->~State();
    free(main_thread_, sizeof(State));
    for (GcObject* list : {finalizable_, to_be_finalized_}) { // empty after close()
        while (list != nullptr) {
            GcObject* next = list->next;
            free_object(list);
            list = next;
        }
    }
    o = fixed_;
    while (o != nullptr) {
        GcObject* next = o->next;
        free_object(o);
        o = next;
    }
}

// ---- Memory ----

void* Heap::allocate(std::size_t size) {
    // Out of memory, this throws std::bad_alloc before anything changed;
    // protected calls and the C++ function boundary turn it into the
    // "not enough memory" error (vm/execute.cpp).
    if (memory_limit_ != 0 && size > memory_limit_ - std::min(memory_limit_, total_bytes()))
        [[unlikely]] {
        throw std::bad_alloc{};
    }
    void* block = pool_.allocate(size);
    debt_ += static_cast<std::ptrdiff_t>(size);
    return block;
}

void Heap::free(void* block, std::size_t size) {
    debt_ -= static_cast<std::ptrdiff_t>(size);
    pool_.free(block, size);
}

void Heap::set_debt(std::ptrdiff_t debt) {
    // Lua's luaE_setdebt: the total stays the same, only how it splits
    // between total_bytes_ and debt_ changes.
    const auto total = static_cast<std::ptrdiff_t>(total_bytes());
    total_bytes_ = total - debt;
    debt_ = debt;
}

State* Heap::new_thread() {
    // lua_newthread: the new thread has the hooks of the running one (its
    // hook function stays the running thread's, in _HOOKKEY).
    State* thread = link_new<State>(allocate(sizeof(State)), ObjectType::Thread);
    set_hook(*thread, running_->hook_mask, running_->base_hook_count);
    return thread;
}

template <typename T>
T* Heap::link_new(void* memory, ObjectType type) {
    // Lua's luaC_newobj: new objects are white, the newest first in the list.
    // The constructor runs first: it value-initializes the header.
    // The collector owns it from here, through the list of all objects.
    T* object = nullptr;
    if constexpr (std::is_same_v<T, State>) {
        object = new (memory) State(*this); // NOLINT(cppcoreguidelines-owning-memory)
    } else {
        object = new (memory) T(); // NOLINT(cppcoreguidelines-owning-memory)
    }
    object->type = type;
    object->marked = current_white();
    object->next = all_;
    all_ = object;
    return object;
}

void Heap::fix(const GcObject* fixed) {
    // Lua's luaC_fix: the object must be the newest one. It becomes gray
    // and old forever, in a list the sweep never looks at.
    GcObject* object = mutable_object(fixed);
    all_ = object->next;
    set_gray(object);
    set_age(object, gc_bits::age_old);
    object->next = fixed_;
    fixed_ = object;
}

// ---- Strings ----

String* Heap::allocate_string(std::size_t length, std::uint32_t hash, bool is_short) {
    // One block: the header, the bytes, and a '\0'.
    void* memory = allocate(sizeof(String) + length + 1);
    // The collector owns it from here, through the list of all objects.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* s = new (memory) String(length, hash, is_short);
    s->type = is_short ? ObjectType::ShortString : ObjectType::LongString;
    s->marked = current_white();
    s->next = all_;
    all_ = s;
    bytes_of(s)[length] = '\0';
    ++string_count_;
    return s;
}

char* Heap::bytes_of(String* s) {
    // Where String::data() reads them: right after the header.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<char*>(s + 1);
}

const String* Heap::new_string(std::string_view bytes) {
    if (bytes.size() > max_short_length) {
        // Long strings are not interned, and their hash waits until it is
        // needed: until then the string keeps the seed in its place.
        String* s = allocate_string(bytes.size(), seed_, false);
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
    String* s = allocate_string(length, seed_, false);
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
    String** bucket = &buckets_[hash & (buckets_.size() - 1)];
    for (String* s = *bucket; s != nullptr; s = s->next_in_bucket_) {
        if (s->length_ == bytes.size() &&
            (bytes.empty() || std::memcmp(s->data(), bytes.data(), bytes.size()) == 0)) {
            // A string the collector found dead but has not freed yet (it
            // is in the sweep phase) comes back to life.
            if (is_dead(s)) {
                s->marked = static_cast<std::uint8_t>(s->marked ^ gc_bits::whites);
            }
            return s;
        }
    }
    if (short_count_ >= buckets_.size()) { // grow first, as Lua's growstrtab
        resize_string_table(buckets_.size() * 2);
        bucket = &buckets_[hash & (buckets_.size() - 1)];
    }
    String* s = allocate_string(bytes.size(), hash, true);
    if (!bytes.empty()) { // (a view of nothing may have a null data())
        std::memcpy(bytes_of(s), bytes.data(), bytes.size());
    }
    s->next_in_bucket_ = *bucket;
    *bucket = s;
    ++short_count_;
    return s;
}

void Heap::resize_string_table(std::size_t size) {
    // Every short string moves to its bucket in the new array (Lua's
    // luaS_resize). The bucket count stays a power of two, so a bucket is
    // found with a mask instead of a division.
    std::vector<String*> resized(size, nullptr);
    for (String* chain : buckets_) {
        while (chain != nullptr) {
            String* next = chain->next_in_bucket_;
            String*& bucket = resized[chain->hash_ & (size - 1)];
            chain->next_in_bucket_ = bucket;
            bucket = chain;
            chain = next;
        }
    }
    count_bytes(static_cast<std::ptrdiff_t>((size - buckets_.size()) * sizeof(String*)));
    buckets_ = std::move(resized);
}

void Heap::remove_string(String* s) {
    // Lua's luaS_remove: unlink a dead short string from its bucket.
    String** p = &buckets_[s->hash_ & (buckets_.size() - 1)];
    while (*p != s) {
        p = &(*p)->next_in_bucket_;
    }
    *p = s->next_in_bucket_;
    --short_count_;
}

// ---- Other objects ----

Proto* Heap::new_proto() {
    return link_new<Proto>(allocate(sizeof(Proto)), ObjectType::Proto);
}

LuaClosure* Heap::new_closure(Proto* proto, std::size_t upvalue_count) {
    void* memory = allocate(LuaClosure::size_for(upvalue_count));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the collector
    auto* closure = new (memory) LuaClosure(upvalue_count);
    closure->marked = current_white();
    closure->next = all_;
    all_ = closure;
    closure->proto = proto;
    for (Upvalue*& upvalue : closure->upvalues()) {
        upvalue = nullptr;
    }
    return closure;
}

Upvalue* Heap::new_upvalue() {
    return link_new<Upvalue>(allocate(sizeof(Upvalue)), ObjectType::Upvalue);
}

Table* Heap::new_table() {
    return link_new<Table>(allocate(sizeof(Table)), ObjectType::Table);
}

Userdata* Heap::new_userdata(std::size_t size, std::size_t user_values) {
    void* memory = allocate(Userdata::size_for(size, user_values));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the collector
    auto* u = new (memory) Userdata(size, static_cast<std::uint16_t>(user_values));
    u->marked = current_white();
    u->next = all_;
    all_ = u;
    for (Value& v : u->user_values()) {
        v = Value::nil();
    }
    return u;
}

CClosure* Heap::new_c_closure(CFunction function, std::size_t upvalue_count) {
    void* memory = allocate(CClosure::size_for(upvalue_count));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the collector
    auto* closure = new (memory) CClosure(upvalue_count);
    closure->marked = current_white();
    closure->next = all_;
    all_ = closure;
    closure->function = function;
    for (Value& v : closure->upvalues()) {
        v = Value::nil();
    }
    return closure;
}

std::int64_t Heap::reference(Value v) {
    std::int64_t key = 0;
    if (free_references_.empty()) {
        key = next_reference_++;
    } else {
        key = free_references_.back();
        free_references_.pop_back();
    }
    references_->set_int(*this, key, v);
    return key;
}

void Heap::release_reference(std::int64_t key) {
    references_->set_int(*this, key, Value::nil());
    free_references_.push_back(key);
}

Value Heap::referenced(std::int64_t key) {
    return references_->get_int(key).value();
}

void Heap::set_metatable(Userdata* u, Table* mt) {
    u->metatable = mt;
    if (mt != nullptr) {
        object_barrier(u, mt);
        check_finalizer(u, mt);
    }
}

void Heap::set_metatable(Table* t, Table* mt) {
    // lua_setmetatable for a table.
    t->metatable = mt;
    if (mt != nullptr) {
        object_barrier(t, mt);
        check_finalizer(t, mt);
    }
}

void Heap::write_error(std::string_view text) {
    if (error_output_) {
        error_output_(text);
        return;
    }
    std::cerr.write(text.data(), static_cast<std::streamsize>(text.size()));
    std::cerr.flush();
}

void Heap::write_output(std::string_view text) {
    if (output_) {
        output_(text);
        return;
    }
    // lua_writestring and lua_writeline: a write, and a flush at the end
    // of each line.
    std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (text.ends_with('\n')) {
        std::cout.flush();
    }
}

void Heap::flush_output() {
    if (!output_) {
        std::cout.flush();
    }
}

void Heap::free_object(GcObject* o) {
    // Lua's freeobj.
    switch (o->type) {
    case ObjectType::ShortString: {
        auto* s = object_cast<String>(o);
        remove_string(s);
        --string_count_;
        const std::size_t size = sizeof(String) + s->length() + 1;
        s->~String();
        free(s, size);
        break;
    }
    case ObjectType::LongString: {
        auto* s = object_cast<String>(o);
        --string_count_;
        const std::size_t size = sizeof(String) + s->length() + 1;
        s->~String();
        free(s, size);
        break;
    }
    case ObjectType::Upvalue: {
        auto* upvalue = object_cast<Upvalue>(o);
        if (upvalue->is_open()) {
            upvalue->unlink();
        }
        upvalue->~Upvalue();
        free(upvalue, sizeof(Upvalue));
        break;
    }
    case ObjectType::Proto: {
        auto* proto = object_cast<Proto>(o);
        count_bytes(-static_cast<std::ptrdiff_t>(proto->counted_bytes));
        proto->~Proto();
        free(proto, sizeof(Proto));
        break;
    }
    case ObjectType::LuaClosure: {
        auto* closure = object_cast<LuaClosure>(o);
        const std::size_t size = LuaClosure::size_for(closure->upvalue_count());
        closure->~LuaClosure();
        free(closure, size);
        break;
    }
    case ObjectType::Table: {
        auto* table = object_cast<Table>(o);
        table->release(*this);
        table->~Table();
        free(table, sizeof(Table));
        break;
    }
    case ObjectType::CClosure: {
        auto* closure = object_cast<CClosure>(o);
        const std::size_t size = CClosure::size_for(closure->upvalue_count());
        closure->~CClosure();
        free(closure, size);
        break;
    }
    case ObjectType::Userdata: {
        auto* u = object_cast<Userdata>(o);
        if (u->destructor != nullptr) {
            u->destructor(u->data()); // the C++ object in the block
        }
        const std::size_t size = Userdata::size_for(u->size(), u->user_values().size());
        u->~Userdata();
        free(u, size);
        break;
    }
    case ObjectType::Thread: {
        // luaE_freethread: open upvalues into its stack close first.
        auto* thread = object_cast<State>(o);
        thread->close_upvalues(0);
        thread->~State();
        free(thread, sizeof(State));
        break;
    }
    }
}

} // namespace cpplua
