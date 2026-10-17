#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "gc/object.hpp"
#include "value/value.hpp"

namespace cpplua {

class Table;

// A full userdata (Lua's Udata): a block of memory that C++ owns the
// meaning of, as a Lua value, with a metatable and `user value` slots for
// Lua values the object refers to (lua_newuserdatauv). The collector frees
// it when it is unreachable; before that, its __gc finalizer runs (if its
// metatable has one when it is set, M7.1), and after it, the `destructor`
// C++ gave it, so a C++ object living in the block is destroyed properly.
//
// Layout, one allocation: this header, the user values, then the data,
// aligned to 16 bytes.
class Userdata : public GcObject {
public:
    using Destructor = void (*)(void* data);

    Userdata(const Userdata&) = delete;
    Userdata& operator=(const Userdata&) = delete;
    Userdata(Userdata&&) = delete;
    Userdata& operator=(Userdata&&) = delete;
    ~Userdata() = default;

    // The bytes a userdata of `size` bytes with `user_values` slots takes.
    static constexpr std::size_t header_size(std::size_t user_values) {
        const std::size_t end = sizeof(Userdata) + (user_values * sizeof(Value));
        return (end + 15) & ~std::size_t{15};
    }

    static constexpr std::size_t size_for(std::size_t size, std::size_t user_values) {
        return header_size(user_values) + size;
    }

    [[nodiscard]] std::size_t size() const { return size_; }

    [[nodiscard]] void* data() {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<char*>(this) + header_size(user_value_count_);
    }

    [[nodiscard]] std::span<Value> user_values() {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return {reinterpret_cast<Value*>(this + 1), user_value_count_};
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes): the API's fast path
    Table* metatable = nullptr;
    GcObject* gc_list = nullptr; // the collector's gray lists
    // Which C++ class lives in the block (the public API's class tag), so
    // a C++ function can check it was given the right kind of object.
    const void* type_tag = nullptr;
    Destructor destructor = nullptr;
    // The block holds a pointer to the C++ object, which lives in another
    // object (a data member of a bound class, Context::push_member), kept
    // alive by user value 2.
    bool holds_pointer = false;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    friend class Heap;

    Userdata(std::size_t size, std::uint16_t user_values)
        : size_(size), user_value_count_(user_values) {
        type = ObjectType::Userdata;
    }

    std::size_t size_;
    std::uint16_t user_value_count_;
};

static_assert(sizeof(Userdata) % alignof(Value) == 0);

inline Value Value::userdata(const Userdata* u) {
    return Value{Tag::Userdata, std::bit_cast<std::uint64_t>(u)};
}

} // namespace cpplua
