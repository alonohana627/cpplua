#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "gc/object.hpp"
#include "value/value.hpp"
#include "vm/proto.hpp"

namespace lua2 {

// A variable of an enclosing function that a closure uses (E7, Lua's UpVal).
//
// While the function that declared the variable is running, the upvalue is
// *open*: the value lives in that function's stack slot, and every closure
// sharing the variable goes through the same upvalue to it (E7.1, E7.2).
// When the slot goes away (the block or the function ends), the upvalue is
// *closed*: the value moves into the upvalue itself (E7.3).
//
// The stack keeps tags and payloads in two arrays, so an upvalue points to
// both halves. Open upvalues are listed in their thread, highest stack slot
// first, and know their slot, so the stack can move and they can follow.
struct Upvalue : GcObject {
    Upvalue() { type = ObjectType::Upvalue; }

    Tag* tag = &closed_tag; // where the value is: in the stack, or in closed_*
    std::uint64_t* payload = &closed_payload;

    // While open: neighbors in the thread's list, and the stack slot.
    Upvalue* next_open = nullptr;
    Upvalue** previous_open = nullptr;
    std::uint32_t level = 0;

    // The value, once closed.
    Tag closed_tag = Tag::Nil;
    std::uint64_t closed_payload = 0;

    [[nodiscard]] bool is_open() const { return tag != &closed_tag; }

    [[nodiscard]] Value value() const { return Value::from_parts(*tag, *payload); }

    void set(Value v) const {
        *tag = v.tag();
        *payload = v.payload();
    }

    // Takes this open upvalue out of its thread's list (luaF_unlinkupval).
    void unlink() const {
        *previous_open = next_open;
        if (next_open != nullptr) {
            next_open->previous_open = previous_open;
        }
    }
};

// A function written in Lua, as a value (Lua's LClosure): a prototype and
// the upvalues it uses, which follow the closure in the same allocation.
class LuaClosure : public GcObject {
public:
    LuaClosure(const LuaClosure&) = delete;
    LuaClosure& operator=(const LuaClosure&) = delete;
    LuaClosure(LuaClosure&&) = delete;
    LuaClosure& operator=(LuaClosure&&) = delete;
    ~LuaClosure() = default;

    // The bytes a closure with `n` upvalues takes.
    static constexpr std::size_t size_for(std::size_t n) {
        return sizeof(LuaClosure) + (n * sizeof(Upvalue*));
    }

    [[nodiscard]] std::size_t upvalue_count() const { return upvalue_count_; }

    [[nodiscard]] std::span<Upvalue*> upvalues() { return {upvalue_array(), upvalue_count_}; }

    [[nodiscard]] std::span<Upvalue* const> upvalues() const {
        return {upvalue_array(), upvalue_count_};
    }

    [[nodiscard]] Upvalue* upvalue(std::size_t i) const { return upvalue_array()[i]; }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): the VM's fast path
    Proto* proto = nullptr;
    GcObject* gc_list = nullptr; // the collector's gray lists
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    friend class Heap;

    explicit LuaClosure(std::size_t upvalue_count)
        : upvalue_count_(static_cast<std::uint8_t>(upvalue_count)) {
        type = ObjectType::LuaClosure;
    }

    // The upvalues follow the closure in its allocation (size_for).
    [[nodiscard]] Upvalue** upvalue_array() {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<Upvalue**>(this + 1);
    }

    [[nodiscard]] Upvalue* const* upvalue_array() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<Upvalue* const*>(this + 1);
    }

    std::uint8_t upvalue_count_;
};

static_assert(sizeof(LuaClosure) % alignof(Upvalue*) == 0);

// A C++ function with upvalues, as a value (Lua's CClosure): the function
// and `n` values it keeps, which follow the closure in its allocation. The
// function reads them through its own frame (`upvalue` in std/auxlib.hpp).
class CClosure : public GcObject {
public:
    CClosure(const CClosure&) = delete;
    CClosure& operator=(const CClosure&) = delete;
    CClosure(CClosure&&) = delete;
    CClosure& operator=(CClosure&&) = delete;
    ~CClosure() = default;

    static constexpr std::size_t size_for(std::size_t n) {
        return sizeof(CClosure) + (n * sizeof(Value));
    }

    [[nodiscard]] std::size_t upvalue_count() const { return upvalue_count_; }

    [[nodiscard]] std::span<Value> upvalues() {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return {reinterpret_cast<Value*>(this + 1), upvalue_count_};
    }

    [[nodiscard]] std::span<const Value> upvalues() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return {reinterpret_cast<const Value*>(this + 1), upvalue_count_};
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): the VM's fast path
    CFunction function = nullptr;
    GcObject* gc_list = nullptr; // the collector's gray lists
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    friend class Heap;

    explicit CClosure(std::size_t upvalue_count)
        : upvalue_count_(static_cast<std::uint8_t>(upvalue_count)) {
        type = ObjectType::CClosure;
    }

    std::uint8_t upvalue_count_;
};

static_assert(sizeof(CClosure) % alignof(Value) == 0);

inline Value Value::c_closure(const CClosure* f) {
    return Value{Tag::CClosure, std::bit_cast<std::uint64_t>(f)};
}

inline Value Value::closure(const LuaClosure* f) {
    return Value{Tag::LuaClosure, std::bit_cast<std::uint64_t>(f)};
}

} // namespace lua2
