#pragma once

#include <cstdint>

namespace lua2 {

// What kind of object a collectable object is (Lua's variant tags for
// collectable types).
enum class ObjectType : std::uint8_t {
    ShortString,
    LongString,
    Upvalue,
    Proto,
    LuaClosure,
    Thread,
    Table,
};

// The header every collectable object starts with (Lua's CommonHeader): the
// link in the collector's list of all objects, the object's type, and the
// collector's mark bits. Objects derive from it, so a pointer to an object
// is a pointer to its header.
struct GcObject {
    GcObject* next = nullptr;
    ObjectType type = ObjectType::ShortString;
    std::uint8_t marked = 0;
};

// The bits of GcObject::marked, as in Lua's lgc.h. The low three bits are the
// object's age in generational mode; then two whites, black, and a bit for
// finalization (stage 7).
namespace gc_bits {

inline constexpr std::uint8_t white0 = 1U << 3U;
inline constexpr std::uint8_t white1 = 1U << 4U;
inline constexpr std::uint8_t black = 1U << 5U;
inline constexpr std::uint8_t finalized = 1U << 6U;
inline constexpr std::uint8_t whites = white0 | white1;
inline constexpr std::uint8_t colors = whites | black;
inline constexpr std::uint8_t age_mask = 7;
inline constexpr std::uint8_t all = colors | age_mask;

// Ages in generational mode (Lua's G_NEW ... G_TOUCHED2).
inline constexpr std::uint8_t age_new = 0;      // created in this cycle
inline constexpr std::uint8_t age_survival = 1; // created in the previous cycle
inline constexpr std::uint8_t age_old0 = 2;     // marked old by a forward barrier this cycle
inline constexpr std::uint8_t age_old1 = 3;     // first full cycle as old
inline constexpr std::uint8_t age_old = 4;      // really old: not visited by minor collections
inline constexpr std::uint8_t age_touched1 = 5; // old object touched this cycle
inline constexpr std::uint8_t age_touched2 = 6; // old object touched in the previous cycle

} // namespace gc_bits

// The three colors of tri-color marking (lgc.h): white objects are not
// marked (yet), gray ones are marked but their references may not be, black
// ones are marked with everything they refer to.

constexpr bool is_white(const GcObject* o) {
    return (o->marked & gc_bits::whites) != 0;
}

constexpr bool is_black(const GcObject* o) {
    return (o->marked & gc_bits::black) != 0;
}

constexpr bool is_gray(const GcObject* o) {
    return (o->marked & gc_bits::colors) == 0;
}

constexpr std::uint8_t age_of(const GcObject* o) {
    return o->marked & gc_bits::age_mask;
}

// `bits` with `mask` cleared, on unsigned values (the bytes would promote
// to int).
constexpr unsigned cleared(std::uint8_t bits, std::uint8_t mask) {
    return static_cast<unsigned>(bits) & ~static_cast<unsigned>(mask);
}

constexpr void set_age(GcObject* o, std::uint8_t age) {
    o->marked = static_cast<std::uint8_t>(cleared(o->marked, gc_bits::age_mask) | age);
}

constexpr bool is_old(const GcObject* o) {
    return age_of(o) > gc_bits::age_survival;
}

// Makes an object gray (neither white nor black), and black from any color.
constexpr void set_gray(GcObject* o) {
    o->marked = static_cast<std::uint8_t>(cleared(o->marked, gc_bits::colors));
}

constexpr void set_black(GcObject* o) {
    o->marked = static_cast<std::uint8_t>(cleared(o->marked, gc_bits::whites) | gc_bits::black);
}

// The object a header belongs to, once its type is known. The collector
// checks `type` first; this is the one place the downcast is spelled.
template <typename T>
T* object_cast(GcObject* o) {
    return static_cast<T*>(o); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
}

// The collector changes the mark bits and gray-list links of objects the
// rest of the program only reads (strings and protos are const to their
// users); this is the one place that removes the const.
inline GcObject* mutable_object(const GcObject* o) {
    return const_cast<GcObject*>(o); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

} // namespace lua2
