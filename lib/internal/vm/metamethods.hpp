#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "value/value.hpp"

namespace lua2 {

class Heap;
class Table;

// The events a metatable can handle (Lua's TMS, in its order: the MMBIN
// instructions carry these numbers, docs/spec/metatables.md).
enum class Event : std::uint8_t {
    Index,
    NewIndex,
    Gc,
    Mode,
    Len,
    Eq, // the last event whose absence a metatable caches
    Add,
    Sub,
    Mul,
    Mod,
    Pow,
    Div,
    IDiv,
    BAnd,
    BOr,
    BXor,
    Shl,
    Shr,
    Unm,
    BNot,
    Lt,
    Le,
    Concat,
    Call,
    Close,
};

inline constexpr std::size_t event_count = static_cast<std::size_t>(Event::Close) + 1;

// The field names, "__index" and so on (luaT_eventname).
inline constexpr std::array<std::string_view, event_count> event_names{
    "__index", "__newindex", "__gc",  "__mode", "__len",    "__eq",   "__add",   "__sub", "__mul",
    "__mod",   "__pow",      "__div", "__idiv", "__band",   "__bor",  "__bxor",  "__shl", "__shr",
    "__unm",   "__bnot",     "__lt",  "__le",   "__concat", "__call", "__close",
};

constexpr std::string_view event_name(Event e) {
    return event_names[static_cast<std::size_t>(e)];
}

// The metatable of a value: a table's own, or the one for its type (none
// until the string library, stage 9). nullptr when there is none.
Table* metatable_of(const Heap& heap, Value v);

// The metamethod for `e` in metatable `mt` (Lua's fasttm and luaT_gettm):
// nil when absent. For the events up to Eq, the metatable remembers an
// absence, so asking again costs one bit test.
Value metamethod_in(const Heap& heap, Table* mt, Event e);

// The metamethod for `e` of value `v` (luaT_gettmbyobj): nil when absent.
Value metamethod_of(const Heap& heap, Value v, Event e);

// A field of v's metatable by name, raw ("__name", "__tostring"): nil when
// absent (luaL_getmetafield).
Value metafield(Heap& heap, Value v, std::string_view name);

// The type name error messages use (luaT_objtypename): a table's `__name`
// when it is a string, else the type's name (M3.7).
std::string_view object_type_name(const Heap& heap, Value v);

} // namespace lua2
