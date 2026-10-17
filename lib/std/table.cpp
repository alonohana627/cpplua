// The table library: a port of Lua 5.4.9's ltablib.c
// (docs/final_docs/stdlib.md). Every access goes through __index,
// __newindex and __len as Lua's does, with fast paths for plain tables.

#include "value/table.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/compare.hpp"
#include "value/convert.hpp"
#include "vm/execute.hpp"
#include "vm/metamethods.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace lua2 {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

// What an object must do to stand in for a table (TAB_R, TAB_W, TAB_L).
constexpr unsigned tab_read = 1;
constexpr unsigned tab_write = 2;
constexpr unsigned tab_length = 4;

// checktab: a table, or a value whose metatable has the metamethods asked for.
bool check_table_like(State& state, int n, unsigned what) {
    const Value v = arg(state, n);
    if (v.is_table()) {
        return true;
    }
    Heap& heap = state.heap();
    if (metatable_of(heap, v) != nullptr &&
        ((what & tab_read) == 0 || !metafield(heap, v, "__index").is_nil()) &&
        ((what & tab_write) == 0 || !metafield(heap, v, "__newindex").is_nil()) &&
        ((what & tab_length) == 0 || !metafield(heap, v, "__len").is_nil())) {
        return true;
    }
    arg_type_error(state, n, "table");
    return false;
}

// aux_getn
bool table_length(State& state, int n, unsigned what, std::int64_t& out) {
    return check_table_like(state, n, what | tab_length) &&
           length_integer(state, arg(state, n), out) == Status::Ok;
}

// A table with no metatable, whose keys 1 to `last` are in its array part:
// the library may move its values directly (nothing can observe the moves).
Table* plain_array(Value t, std::int64_t last) {
    if (!t.is_table()) {
        return nullptr;
    }
    Table* table = table_object(t);
    return table->metatable == nullptr && last >= 0 &&
                   std::cmp_less_equal(last, table->array_limit())
               ? table
               : nullptr;
}

// t[to .. to + count - 1] = t[from .. from + count - 1] in the array part,
// as Lua's loop of lua_geti and lua_seti would leave it.
void move_array(Table& table, std::int64_t from, std::int64_t to, std::int64_t count) {
    if (count <= 0) {
        return;
    }
    const auto n = static_cast<std::size_t>(count);
    std::memmove(table.array_tags() + to - 1, table.array_tags() + from - 1, n * sizeof(Tag));
    std::memmove(table.array_values() + to - 1, table.array_values() + from - 1,
                 n * sizeof(std::uint64_t));
}

std::int64_t wrapping_add(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}

int table_insert(State& state) {
    std::int64_t e = 0;
    if (!table_length(state, 1, tab_read | tab_write, e)) {
        return c_function_error;
    }
    e = wrapping_add(e, 1); // first empty element
    const Value t = arg(state, 1);
    std::int64_t pos = 0;
    switch (arg_count(state)) {
    case 2: pos = e; break;
    case 3: {
        if (!check_integer(state, 2, pos)) {
            return c_function_error;
        }
        if (static_cast<std::uint64_t>(pos) - 1U >= static_cast<std::uint64_t>(e)) {
            return arg_error(state, 2, "position out of bounds");
        }
        std::int64_t i = e;
        if (i > pos) { // t[e] = t[e - 1] may grow the table: done as Lua does
            Value v;
            if (get_index(state, t, i - 1, v) != Status::Ok ||
                set_index(state, t, i, v) != Status::Ok) {
                return c_function_error;
            }
            --i;
        }
        if (Table* table = plain_array(t, i); table != nullptr && i > pos) {
            move_array(*table, pos, pos + 1, i - pos);
            i = pos;
        }
        for (; i > pos; --i) { // move up elements
            Value v;
            if (get_index(state, t, i - 1, v) != Status::Ok ||
                set_index(state, t, i, v) != Status::Ok) {
                return c_function_error;
            }
        }
        break;
    }
    default: return raise_error(state, "wrong number of arguments to 'insert'");
    }
    if (set_index(state, t, pos, arg(state, arg_count(state))) != Status::Ok) {
        return c_function_error;
    }
    return 0;
}

int table_remove(State& state) {
    std::int64_t size = 0;
    if (!table_length(state, 1, tab_read | tab_write, size)) {
        return c_function_error;
    }
    std::int64_t pos = 0;
    if (!opt_integer(state, 2, size, pos)) {
        return c_function_error;
    }
    if (pos != size && static_cast<std::uint64_t>(pos) - 1U > static_cast<std::uint64_t>(size)) {
        return arg_error(state, 2, "position out of bounds");
    }
    const Value t = arg(state, 1);
    Value result;
    if (get_index(state, t, pos, result) != Status::Ok) {
        return c_function_error;
    }
    push(state, result);
    if (Table* table = plain_array(t, size); table != nullptr && pos >= 1 && pos < size) {
        move_array(*table, pos + 1, pos, size - pos);
        pos = size;
    }
    for (; pos < size; ++pos) {
        Value v;
        if (get_index(state, t, pos + 1, v) != Status::Ok ||
            set_index(state, t, pos, v) != Status::Ok) {
            return c_function_error;
        }
    }
    if (set_index(state, t, pos, Value::nil()) != Status::Ok) {
        return c_function_error;
    }
    return 1;
}

int table_move(State& state) {
    std::int64_t f = 0;
    std::int64_t e = 0;
    std::int64_t t = 0;
    if (!check_integer(state, 2, f) || !check_integer(state, 3, e) || !check_integer(state, 4, t)) {
        return c_function_error;
    }
    const int tt = arg_is_none_or_nil(state, 5) ? 1 : 5; // destination table
    if (!check_table_like(state, 1, tab_read) || !check_table_like(state, tt, tab_write)) {
        return c_function_error;
    }
    const Value source = arg(state, 1);
    const Value destination = arg(state, tt);
    if (e >= f) {
        constexpr std::int64_t max_integer = std::numeric_limits<std::int64_t>::max();
        if (f <= 0 && e >= max_integer + f) {
            return arg_error(state, 3, "too many elements to move");
        }
        const std::int64_t n = e - f + 1; // number of elements to move
        if (t > max_integer - n + 1) {
            return arg_error(state, 4, "destination wrap around");
        }
        bool same = tt == 1;
        if (!same && equal_values(state, source, destination, same) != Status::Ok) {
            return c_function_error;
        }
        const auto copy = [&](std::int64_t i) {
            Value v;
            return get_index(state, source, f + i, v) == Status::Ok &&
                   set_index(state, destination, t + i, v) == Status::Ok;
        };
        if (t > e || t <= f || !same) {
            for (std::int64_t i = 0; i < n; ++i) {
                if (!copy(i)) {
                    return c_function_error;
                }
            }
        } else {
            for (std::int64_t i = n - 1; i >= 0; --i) {
                if (!copy(i)) {
                    return c_function_error;
                }
            }
        }
    }
    push(state, destination);
    return 1;
}

// addfield
bool add_field(State& state, std::string& b, Value t, std::int64_t i) {
    Value v;
    if (get_index(state, t, i, v) != Status::Ok) {
        return false;
    }
    if (v.is_string()) {
        b += v.as_string()->view();
    } else if (v.is_number()) {
        b += display_text(v);
    } else {
        raise_error(state, std::format("invalid value ({}) at index {} in table for 'concat'",
                                       type_name(v.type()), i));
        return false;
    }
    return true;
}

int table_concat(State& state) {
    std::int64_t last = 0;
    std::string_view sep;
    std::int64_t i = 0;
    if (!table_length(state, 1, tab_read, last) || !opt_view(state, 2, "", sep) ||
        !opt_integer(state, 3, 1, i) || !opt_integer(state, 4, last, last)) {
        return c_function_error;
    }
    const Value t = arg(state, 1);
    std::string b;
    for (; i < last; ++i) {
        if (!add_field(state, b, t, i)) {
            return c_function_error;
        }
        b += sep;
    }
    if (i == last && !add_field(state, b, t, i)) { // the last value, if any
        return c_function_error;
    }
    push_string(state, b);
    return 1;
}

int table_pack(State& state) {
    const int n = arg_count(state);
    Heap& heap = state.heap();
    Table* t = heap.new_table();
    t->resize(heap, static_cast<std::uint32_t>(n), 1);
    for (int i = 1; i <= n; ++i) {
        t->set_int(heap, i, arg(state, i));
    }
    t->set(heap, Value::string(heap.new_string("n")), Value::integer(n));
    push(state, Value::table(t));
    return 1;
}

int table_unpack(State& state) {
    std::int64_t i = 0;
    std::int64_t e = 0;
    if (!opt_integer(state, 2, 1, i)) {
        return c_function_error;
    }
    if (arg_is_none_or_nil(state, 3)) {
        if (length_integer(state, arg(state, 1), e) != Status::Ok) {
            return c_function_error;
        }
    } else if (!check_integer(state, 3, e)) {
        return c_function_error;
    }
    if (i > e) {
        return 0; // empty range
    }
    // The number of elements minus 1, without overflows.
    std::uint64_t n = static_cast<std::uint64_t>(e) - static_cast<std::uint64_t>(i);
    if (n >= static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
        !state.check_stack(static_cast<std::uint32_t>(n + 1))) {
        return raise_error(state, "too many results to unpack");
    }
    ++n; // the number of elements
    const Value t = arg(state, 1);
    for (; i < e; ++i) {
        Value v;
        if (get_index(state, t, i, v) != Status::Ok) {
            return c_function_error;
        }
        push(state, v);
    }
    Value v;
    if (get_index(state, t, e, v) != Status::Ok) {
        return c_function_error;
    }
    push(state, v);
    return static_cast<int>(n);
}

// ==== Quicksort (Sedgewick's, as Lua's) ====

using Index = unsigned int; // IdxT

constexpr Index random_limit = 100; // RANLIMIT

// l_randomizePivot: time and clock as sources of "randomness".
unsigned int randomize_pivot() {
    const auto c = static_cast<std::uint64_t>(std::clock());
    const auto t = static_cast<std::uint64_t>(std::time(nullptr));
    return static_cast<unsigned int>(c) + static_cast<unsigned int>(c >> 32U) +
           static_cast<unsigned int>(t) + static_cast<unsigned int>(t >> 32U);
}

// Where a sort reads, writes and compares: the table itself, through
// metamethods and the order function. The values it holds between table
// accesses (the pivot and the two it compares) also sit in stack slots, so
// the collector sees them while the order function runs, as Lua keeps them
// on its stack.
struct TableStore {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during the sort
    State& state;
    Value t;
    Value order;           // nil: <
    std::uint32_t slots{}; // the pivot, a[i], a[j]

    bool get(Index i, Value& out, std::uint32_t slot) {
        if (get_index(state, t, i, out) != Status::Ok) {
            return false;
        }
        state.set(slots + slot, out);
        return true;
    }

    bool set(Index i, Value v) { return set_index(state, t, i, v) == Status::Ok; }

    // set2: t[i] = a; t[j] = b.
    bool set2(Index i, Value a, Index j, Value b) { return set(i, a) && set(j, b); }

    // sort_comp: a < b in the sort's order.
    bool less(Value a, Value b, bool& result) {
        if (order.is_nil()) {
            return compare_values(state, a, b, false, result) == Status::Ok;
        }
        const std::uint32_t func = state.top;
        state.set(func, order);
        state.set(func + 1, a);
        state.set(func + 2, b);
        state.top = func + 3;
        if (call(state, func, 1) != Status::Ok) {
            return false;
        }
        result = state.get(func).is_truthy();
        state.top = func;
        return true;
    }

    bool order_error() {
        raise_error(state, "invalid order function for sorting");
        return false;
    }
};

// A plain array of numbers, or of strings, sorted with <: a copy of its
// values, sorted in place by the same steps (so the result is the same,
// equal elements included), then written back. No metamethod or Lua code
// can run, so nothing sees the table meanwhile.
struct ArrayStore {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during the sort
    State& state;
    std::vector<Value> values; // values[i - 1] is a[i]

    bool get(Index i, Value& out, std::uint32_t /*slot*/) {
        out = values[i - 1];
        return true;
    }

    bool set(Index i, Value v) {
        values[i - 1] = v;
        return true;
    }

    bool set2(Index i, Value a, Index j, Value b) {
        values[i - 1] = a;
        values[j - 1] = b;
        return true;
    }

    static bool less(Value a, Value b, bool& result) {
        if (a.is_integer() && b.is_integer()) {
            result = a.as_integer() < b.as_integer();
        } else if (a.is_float() && b.is_float()) {
            result = a.as_float() < b.as_float();
        } else {
            result = less_than(a, b).value_or(false); // numbers or strings: no errors
        }
        return true;
    }

    bool order_error() {
        raise_error(state, "invalid order function for sorting");
        return false;
    }
};

// Lua's quicksort over a store.
template <class Store>
struct Sorter {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during the sort
    Store& store;

    bool get(Index i, Value& out, std::uint32_t slot) { return store.get(i, out, slot); }

    bool set2(Index i, Value a, Index j, Value b) { return store.set2(i, a, j, b); }

    bool less(Value a, Value b, bool& result) { return store.less(a, b, result); }

    bool order_error() { return store.order_error(); }

    // partition: the pivot P is a[up - 1]; a[lo] <= P <= a[up]. Gives i
    // with a[lo .. i - 1] <= a[i] == P <= a[i + 1 .. up].
    bool partition(Index lo, Index up, Value pivot, Index& out) {
        Index i = lo;     // incremented before first use
        Index j = up - 1; // decremented before first use
        for (;;) {
            Value ai;
            Value aj;
            bool lt = false;
            // repeat ++i while a[i] < P
            for (;;) {
                if (!get(++i, ai, 1) || !less(ai, pivot, lt)) {
                    return false;
                }
                if (!lt) {
                    break;
                }
                if (i == up - 1) { // a[i] < P but a[up - 1] == P ??
                    return order_error();
                }
            }
            // repeat --j while P < a[j]
            for (;;) {
                if (!get(--j, aj, 2) || !less(pivot, aj, lt)) {
                    return false;
                }
                if (!lt) {
                    break;
                }
                if (j < i) { // j < i but a[j] > P ??
                    return order_error();
                }
            }
            if (j < i) { // no elements out of place
                if (!set2(up - 1, ai, i, pivot)) {
                    return false;
                }
                out = i;
                return true;
            }
            if (!set2(i, aj, j, ai)) {
                return false;
            }
        }
    }

    // choosePivot: an element in the middle half of [lo, up].
    static Index choose_pivot(Index lo, Index up, unsigned int rnd) {
        const Index r4 = (up - lo) / 4;
        return (rnd % (r4 * 2)) + (lo + r4);
    }

    // auxsort
    bool sort(Index lo, Index up, unsigned int rnd) { // NOLINT(misc-no-recursion)
        while (lo < up) {                             // a loop for tail recursion
            Value a;
            Value b;
            bool lt = false;
            // sort a[lo], a[p], a[up]
            if (!get(lo, a, 1) || !get(up, b, 2) || !less(b, a, lt)) {
                return false;
            }
            if (lt && !set2(lo, b, up, a)) {
                return false;
            }
            if (up - lo == 1) { // only 2 elements
                return true;
            }
            Index p =
                up - lo < random_limit || rnd == 0 ? (lo + up) / 2 : choose_pivot(lo, up, rnd);
            if (!get(p, a, 1) || !get(lo, b, 2) || !less(a, b, lt)) {
                return false;
            }
            if (lt) {
                if (!set2(p, b, lo, a)) {
                    return false;
                }
            } else {
                if (!get(up, b, 2) || !less(b, a, lt)) {
                    return false;
                }
                if (lt && !set2(p, b, up, a)) {
                    return false;
                }
            }
            if (up - lo == 2) { // only 3 elements
                return true;
            }
            Value pivot;
            if (!get(p, pivot, 0) || !get(up - 1, b, 2) || !set2(p, b, up - 1, pivot)) {
                return false;
            }
            if (!partition(lo, up, pivot, p)) {
                return false;
            }
            Index n = 0;
            if (p - lo < up - p) { // the lower interval is smaller
                if (!sort(lo, p - 1, rnd)) {
                    return false;
                }
                n = p - lo;
                lo = p + 1;
            } else {
                if (!sort(p + 1, up, rnd)) {
                    return false;
                }
                n = up - p;
                up = p - 1;
            }
            if ((up - lo) / 128 > n) { // partition too imbalanced
                rnd = randomize_pivot();
            }
        }
        return true;
    }
};

int table_sort(State& state) {
    std::int64_t n = 0;
    if (!table_length(state, 1, tab_read | tab_write, n)) {
        return c_function_error;
    }
    if (n > 1) {
        if (n >= std::numeric_limits<int>::max()) {
            return arg_error(state, 1, "array too big");
        }
        if (!arg_is_none_or_nil(state, 2) && !check_function(state, 2)) {
            return c_function_error;
        }
        const Value t = arg(state, 1);
        const Value order = arg(state, 2);
        if (Table* table = plain_array(t, n); table != nullptr && order.is_nil()) {
            // All numbers or all strings: sort a copy.
            ArrayStore store{.state = state, .values = {}};
            store.values.reserve(static_cast<std::size_t>(n));
            bool numbers = true;
            bool strings = true;
            for (std::int64_t i = 1; i <= n; ++i) {
                const Value v = table->get_int(i).value();
                numbers = numbers && v.is_number();
                strings = strings && v.is_string();
                store.values.push_back(v);
            }
            if (numbers || strings) {
                Sorter<ArrayStore> sorter{.store = store};
                const bool ok = sorter.sort(1, static_cast<Index>(n), 0);
                for (std::int64_t i = 1; i <= n; ++i) { // also after an error, as Lua leaves it
                    table->get_int(i).store(store.values[static_cast<std::size_t>(i - 1)]);
                }
                return ok ? 0 : c_function_error;
            }
        }
        // Three slots above the arguments for the values the sort holds.
        TableStore store{.state = state, .t = t, .order = order, .slots = state.top};
        state.top += 3;
        Sorter<TableStore> sorter{.store = store};
        if (!sorter.sort(1, static_cast<Index>(n), 0)) {
            return c_function_error;
        }
    }
    return 0;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array table_functions{
    LibraryFunction{.name = "concat", .function = &table_concat},
    LibraryFunction{.name = "insert", .function = &table_insert},
    LibraryFunction{.name = "pack", .function = &table_pack},
    LibraryFunction{.name = "unpack", .function = &table_unpack},
    LibraryFunction{.name = "remove", .function = &table_remove},
    LibraryFunction{.name = "move", .function = &table_move},
    LibraryFunction{.name = "sort", .function = &table_sort},
};

} // namespace

void open_table(Heap& heap) {
    open_library(heap, "table", table_functions);
}

} // namespace lua2
