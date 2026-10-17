// Metamethod lookup: a port of Lua 5.4's ltm.c (docs/final_docs/metatables.md).

#include "vm/metamethods.hpp"

#include "gc/heap.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"

namespace lua2 {

Table* metatable_of(const Heap& heap, Value v) {
    if (v.is_table()) {
        return v.as_table()->metatable;
    }
    if (v.is_userdata()) {
        return v.as_userdata()->metatable;
    }
    return heap.type_metatable(v.type());
}

Value metamethod_in(const Heap& heap, Table* mt, Event e) {
    if (mt == nullptr) {
        return Value::nil();
    }
    const unsigned bit = 1U << static_cast<unsigned>(e);
    const bool cached = e <= Event::Eq;
    if (cached && mt->knows_absent(bit)) {
        return Value::nil();
    }
    const Value tm = mt->get_short_string(heap.event_string(e)).value();
    if (tm.is_nil() && cached) {
        mt->set_absent(bit);
    }
    return tm;
}

Value metamethod_of(const Heap& heap, Value v, Event e) {
    Table* mt = metatable_of(heap, v);
    if (mt == nullptr) {
        return Value::nil();
    }
    return mt->get_short_string(heap.event_string(e)).value();
}

Value metafield(Heap& heap, Value v, std::string_view name) {
    Table* mt = metatable_of(heap, v);
    if (mt == nullptr) {
        return Value::nil();
    }
    return mt->get_string(heap.new_string(name)).value();
}

std::string_view object_type_name(const Heap& heap, Value v) {
    if (v.is_table() || v.is_userdata()) {
        if (Table* mt = metatable_of(heap, v)) {
            const Value name = mt->get_short_string(heap.name_string()).value();
            if (name.is_string()) {
                return name.as_string()->view();
            }
        }
    }
    return type_name(v.type());
}

} // namespace lua2
