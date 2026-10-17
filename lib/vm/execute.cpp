// The interpreter: Lua 5.4's luaV_execute and the call machinery of ldo.c
// (docs/final_docs/vm.md). The instruction bodies are in handlers.inc,
// written once; this file puts them in one of two dispatch styles:
//
// - tail calls (the default with clang): each instruction is a function that
//   ends by jumping straight into the next instruction's function, with the
//   VM's state passed in registers ([[clang::musttail]] with the
//   preserve_none calling convention, measured fastest in
//   docs/learn/vm_gc.md);
// - a switch in a loop, for compilers without those (or LUA2_DISPATCH=switch).
//
// The hot paths are inline in the bodies; everything rare (errors,
// metamethods, stack growth, collection) is in the cold functions below, out
// of the way.
//
// Errors are return values, never C++ exceptions: a failing instruction
// returns Status::Error with the error object in State::error_value, and so
// does every call above it, until a protected call (pcall) takes it
// (docs/final_docs/errors.md).

#include "vm/execute.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "std/base.hpp"
#include "value/arith.hpp"
#include "value/bitwise.hpp"
#include "value/compare.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "vm/debug_info.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/opcodes.hpp"
#include "vm/proto.hpp"

// NOLINTBEGIN(cppcoreguidelines-macro-usage): a choice for the preprocessor
#if defined(LUA2_DISPATCH_SWITCH) || !defined(__clang__)
#define LUA2_TAIL_CALLS 0
#else
#define LUA2_TAIL_CALLS 1
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace lua2 {

namespace {

// ---- Registers ----

constexpr std::uint32_t u32(int i) {
    return static_cast<std::uint32_t>(i);
}

const LuaClosure* closure_of(const std::uint64_t* vals) {
    return std::bit_cast<const LuaClosure*>(vals[-1]); // the function's own slot
}

std::int64_t as_int(std::uint64_t payload) {
    return std::bit_cast<std::int64_t>(payload);
}

double as_float(std::uint64_t payload) {
    return std::bit_cast<double>(payload);
}

void set_integer(Tag* tags, std::uint64_t* vals, int r, std::int64_t i) {
    tags[r] = Tag::Integer;
    vals[r] = std::bit_cast<std::uint64_t>(i);
}

void set_float(Tag* tags, std::uint64_t* vals, int r, double f) {
    tags[r] = Tag::Float;
    vals[r] = std::bit_cast<std::uint64_t>(f);
}

// The stack slot of the running function's first register.
std::uint32_t base_index(const State* state, const Tag* tags) {
    return static_cast<std::uint32_t>(tags - state->stack_tags);
}

Value register_value(const Tag* tags, const std::uint64_t* vals, int r) {
    return Value::from_parts(tags[r], vals[r]);
}

void set_register(Tag* tags, std::uint64_t* vals, int r, Value v) {
    tags[r] = v.tag();
    vals[r] = v.payload();
}

// A table value's object, which the VM may change.
Table* table_of(std::uint64_t payload) {
    return std::bit_cast<Table*>(payload);
}

Table* table_of(Value v) {
    return std::bit_cast<Table*>(v.payload());
}

// tointegerns: an integer, or a float with an integer value (V4.2).
bool to_integer_strict(Tag tag, std::uint64_t payload, std::int64_t& out) {
    if (tag == Tag::Integer) [[likely]] {
        out = as_int(payload);
        return true;
    }
    if (tag == Tag::Float) {
        if (const auto i = float_to_int(as_float(payload))) {
            out = *i;
            return true;
        }
    }
    return false;
}

double add_floats(double x, double y) {
    return x + y;
}

double subtract_floats(double x, double y) {
    return x - y;
}

double multiply_floats(double x, double y) {
    return x * y;
}

double divide_floats(double x, double y) {
    return x / y;
}

// Both operands' tags in one number, to choose a path with one compare.
constexpr unsigned tag_pair(Tag a, Tag b) {
    return (static_cast<unsigned>(a) << 4U) | static_cast<unsigned>(b);
}

static_assert(static_cast<unsigned>(last_tag) < 16, "tag_pair needs every tag below 16");

constexpr unsigned integer_integer = tag_pair(Tag::Integer, Tag::Integer);
constexpr unsigned float_float = tag_pair(Tag::Float, Tag::Float);
constexpr unsigned integer_float = tag_pair(Tag::Integer, Tag::Float);
constexpr unsigned float_integer = tag_pair(Tag::Float, Tag::Integer);

// Payloads are loaded as the type the path needs: a float goes straight
// from memory into a float register, without a trip through an integer
// register, which would lengthen the chain of dependent float operations.
std::int64_t load_int(const std::uint64_t* p) {
    return std::bit_cast<std::int64_t>(*p);
}

double load_float(const std::uint64_t* p) {
    double d = 0;
    std::memcpy(&d, p, sizeof d);
    return d;
}

// The two operands as floats when both are numbers (tonumberns twice).
[[gnu::always_inline]] inline bool float_operands(unsigned pair, const std::uint64_t* pb,
                                                  const std::uint64_t* pc, double& x, double& y) {
    switch (pair) {
    case float_float:
        x = load_float(pb);
        y = load_float(pc);
        return true;
    case integer_float:
        x = static_cast<double>(load_int(pb));
        y = load_float(pc);
        return true;
    case float_integer:
        x = load_float(pb);
        y = static_cast<double>(load_int(pc));
        return true;
    case integer_integer:
        x = static_cast<double>(load_int(pb));
        y = static_cast<double>(load_int(pc));
        return true;
    default: return false;
    }
}

// op_arith: integers with `int_op`, other numbers as floats with
// `float_op`. False when an operand is not a number (the MMBIN after it
// takes over).
template <typename IntOp, typename FloatOp>
[[gnu::always_inline]] inline bool arith(Tag* tags, std::uint64_t* vals, int a, Tag tb,
                                         const std::uint64_t* pb, Tag tc, const std::uint64_t* pc,
                                         IntOp int_op, FloatOp float_op) {
    const unsigned pair = tag_pair(tb, tc);
    if (pair == integer_integer) [[likely]] {
        set_integer(tags, vals, a, int_op(load_int(pb), load_int(pc)));
        return true;
    }
    double x = 0;
    double y = 0;
    if (float_operands(pair, pb, pc, x, y)) {
        set_float(tags, vals, a, float_op(x, y));
        return true;
    }
    return false;
}

// op_arithf: always in floats (/ and ^).
template <typename FloatOp>
[[gnu::always_inline]] inline bool float_arith(Tag* tags, std::uint64_t* vals, int a, Tag tb,
                                               const std::uint64_t* pb, Tag tc,
                                               const std::uint64_t* pc, FloatOp float_op) {
    double x = 0;
    double y = 0;
    if (float_operands(tag_pair(tb, tc), pb, pc, x, y)) {
        set_float(tags, vals, a, float_op(x, y));
        return true;
    }
    return false;
}

// floatforloop
bool float_for_loop(Tag* tags, std::uint64_t* vals, int a) {
    const double step = as_float(vals[a + 2]);
    const double limit = as_float(vals[a + 1]);
    const double index = as_float(vals[a]) + step;
    if (0 < step ? index <= limit : limit <= index) {
        vals[a] = std::bit_cast<std::uint64_t>(index);
        set_float(tags, vals, a + 3, index);
        return true;
    }
    return false;
}

// ---- Errors ----

const Proto& proto_of(const State* state, const CallInfo* frame) {
    return *std::bit_cast<const LuaClosure*>(state->stack_values[frame->func])->proto;
}

const Proto& running_proto(const State* state) {
    return proto_of(state, state->ci);
}

// The index of the instruction a Lua frame is running (currentpc): its
// saved pc already points past it.
int frame_pc(const State* state, const CallInfo* frame) {
    return static_cast<int>(frame->saved_pc - proto_of(state, frame).code.data()) - 1;
}

// Lua's savepc and savestate (Protect): the frame's pc saved for messages
// and tracebacks, and the top above every register, so that a call (a
// metamethod, a collection's finalizer) goes above them.
void protect(State* state, const Instruction* pc) {
    state->ci->saved_pc = pc;
    state->top = state->ci->top;
}

// luaG_runerror from an instruction.
[[gnu::cold, gnu::noinline]] Status raise(State* state, const Instruction* pc,
                                          std::string_view message) {
    state->ci->saved_pc = pc;
    return runtime_error(*state, message);
}

std::string register_info_here(const State* state, int reg) {
    return register_info(running_proto(state), frame_pc(state, state->ci), reg);
}

// typeerror: "attempt to <op> a <type> value<info>", the type named by
// __name if it has one (M3.7).
[[gnu::cold, gnu::noinline]] Status type_error(State* state, Value value, std::string_view op,
                                               const std::string& info) {
    return runtime_error(*state, std::format("attempt to {} a {} value{}", op,
                                             object_type_name(state->heap(), value), info));
}

// luaG_callerror: named after how the running Lua function made the call
// (E6.3), else nothing.
[[gnu::cold, gnu::noinline]] Status call_error(State* state, Value value) {
    std::string info;
    if (state->is_lua(state->ci)) {
        info = called_info(running_proto(state), frame_pc(state, state->ci));
    }
    return type_error(state, value, "call", info);
}

// Where an indexed value came from, for "attempt to index" (varinfo).
struct Source {
    enum class Kind : std::uint8_t { None, Register, Upvalue };
    Kind kind = Kind::None;
    int index = 0;
};

[[gnu::cold, gnu::noinline]] Status index_error(State* state, Value value, Source source) {
    std::string info;
    if (source.kind == Source::Kind::Register) {
        info = register_info_here(state, source.index);
    } else if (source.kind == Source::Kind::Upvalue) {
        info = upvalue_info(running_proto(state), source.index);
    }
    return type_error(state, value, "index", info);
}

[[gnu::cold, gnu::noinline]] Status not_closable(State* state, const Instruction* pc, int reg) {
    state->ci->saved_pc = pc;
    const String* name = local_name(running_proto(state), reg + 1, frame_pc(state, state->ci));
    return runtime_error(*state,
                         std::format("variable '{}' got a non-closable value",
                                     name != nullptr ? name->view() : std::string_view{"?"}));
}

// A stack that could not grow: "stack overflow" with the position of the
// running function, or "error in error handling" as it is (E6.7, R3).
[[gnu::cold, gnu::noinline]] Status stack_error(State* state) {
    const Value error = state->error_value;
    if (error.is_string() && error.as_string()->view() == "stack overflow") {
        return runtime_error(*state, "stack overflow");
    }
    return Status::Error;
}

[[gnu::cold, gnu::noinline]] Status stack_overflow(State* state, const Instruction* pc) {
    state->ci->saved_pc = pc;
    return stack_error(state);
}

// ---- Metamethods (ltm.c) ----

// metamethod_in inlined, for the VM's paths through __index and
// __newindex (fasttm): one bit test when known absent, else one lookup.
[[gnu::always_inline]] inline Value fast_metamethod(const Heap& heap, Table* mt, Event e) {
    if (mt == nullptr) {
        return Value::nil();
    }
    const unsigned bit = 1U << static_cast<unsigned>(e);
    if (mt->knows_absent(bit)) {
        return Value::nil();
    }
    const Value tm = mt->get_short_string(heap.event_string(e)).value();
    if (tm.is_nil()) {
        mt->set_absent(bit);
    }
    return tm;
}

// luaT_callTM: f(p1, p2, p3) and no results.
Status call_metamethod3(State* state, Value f, Value p1, Value p2, Value p3) {
    const std::uint32_t func = state->top; // EXTRA_STACK has room for four
    state->set(func, f);
    state->set(func + 1, p1);
    state->set(func + 2, p2);
    state->set(func + 3, p3);
    state->top = func + 4;
    return call(*state, func, 0);
}

constexpr int max_tag_loop = 2000; // MAXTAGLOOP

// luaV_finishget: t[key] when the fast path failed (M2.1). The result goes
// to stack slot `result`.
Status finish_get(State* state, Value t, Value key, std::uint32_t result, Source source) {
    const Heap& heap = state->heap();
    for (int loop = 0; loop < max_tag_loop; ++loop) {
        Value tm;
        if (t.is_table()) {
            Table* h = table_of(t);
            const Table::Slot slot = h->get(key);
            if (!slot.empty()) {
                state->set(result, slot.value());
                return Status::Ok;
            }
            tm = fast_metamethod(heap, h->metatable, Event::Index);
            if (tm.is_nil()) {
                state->set(result, Value::nil());
                return Status::Ok;
            }
        } else {
            tm = metamethod_of(heap, t, Event::Index);
            if (tm.is_nil()) {
                return index_error(state, t, source);
            }
        }
        if (tm.is_function()) {
            Value value;
            if (call_metamethod(*state, tm, t, key, value) != Status::Ok) {
                return Status::Error;
            }
            state->set(result, value);
            return Status::Ok;
        }
        t = tm; // try tm[key]
        source = {};
    }
    return runtime_error(*state, "'__index' chain too long; possible loop");
}

// luaV_finishset: t[key] = value when the fast path failed (M2.2).
Status finish_set(State* state, Value t, Value key, Value value, Source source) {
    Heap& heap = state->heap();
    for (int loop = 0; loop < max_tag_loop; ++loop) {
        Value tm;
        if (t.is_table()) {
            Table* h = table_of(t);
            const Table::Slot slot = h->get(key);
            if (!slot.empty()) {
                slot.store(value);
                heap.barrier_back(h, value);
                return Status::Ok;
            }
            tm = fast_metamethod(heap, h->metatable, Event::NewIndex);
            if (tm.is_nil()) {
                if (const char* error = Table::check_key(key)) {
                    return runtime_error(*state, error);
                }
                h->finish_set(heap, key, slot, value);
                return Status::Ok;
            }
        } else {
            tm = metamethod_of(heap, t, Event::NewIndex);
            if (tm.is_nil()) {
                return index_error(state, t, source);
            }
        }
        if (tm.is_function()) {
            return call_metamethod3(state, tm, t, key, value);
        }
        t = tm; // repeat the assignment over tm
        source = {};
    }
    return runtime_error(*state, "'__newindex' chain too long; possible loop");
}

// ---- Slow paths of arithmetic (luaT_trybinTM) ----

constexpr int no_register = -1;

// An operand and the register it is in, for messages.
struct Operand {
    Value value;
    int reg = no_register;
};

constexpr int tm_band = static_cast<int>(Event::BAnd);
constexpr int tm_shr = static_cast<int>(Event::Shr);
constexpr int tm_unm = static_cast<int>(Event::Unm);
constexpr int tm_bnot = static_cast<int>(Event::BNot);

ArithResult arith_event(int event, Value a, Value b) {
    switch (static_cast<Event>(event)) {
    case Event::Add: return add(a, b);
    case Event::Sub: return subtract(a, b);
    case Event::Mul: return multiply(a, b);
    case Event::Mod: return modulo(a, b);
    case Event::Pow: return power(a, b);
    case Event::Div: return divide(a, b);
    case Event::IDiv: return floor_divide(a, b);
    case Event::BAnd: return bit_and(a, b);
    case Event::BOr: return bit_or(a, b);
    case Event::BXor: return bit_xor(a, b);
    case Event::Shl: return shift_left(a, b);
    case Event::Shr: return shift_right(a, b);
    case Event::Unm: return negate(a);
    case Event::BNot: return bit_not(a);
    default: break;
    }
    return std::unexpected(RuntimeError{"invalid metamethod event"});
}

bool is_bitwise(int event) {
    return (event >= tm_band && event <= tm_shr) || event == tm_bnot;
}

std::string operand_info(const State* state, const Operand& o) {
    return o.reg == no_register ? std::string{} : register_info_here(state, o.reg);
}

// The error of a failed arithmetic or bitwise operation with no
// metamethod, as luaT_trybinTM raises it: luaG_opinterror or
// luaG_tointerror name the wrong operand.
[[gnu::cold, gnu::noinline]] Status binary_error(State* state, const Operand& p1, const Operand& p2,
                                                 int event) {
    const bool bitwise = is_bitwise(event);
    if (bitwise && p1.value.is_number() && p2.value.is_number()) {
        const Operand& culprit = to_integer(p1.value) ? p2 : p1;
        return runtime_error(*state, std::format("number{} has no integer representation",
                                                 operand_info(state, culprit)));
    }
    const Operand& culprit = p1.value.is_number() ? p2 : p1;
    return type_error(state, culprit.value,
                      bitwise ? "perform bitwise operation on" : "perform arithmetic on",
                      operand_info(state, culprit));
}

// An arithmetic or bitwise operation on anything but plain numbers, in
// Lua's order (luaT_trybinTM): the first operand's metamethod, then the
// second's. Strings stand in for the string library's arithmetic
// metamethods (lstrlib's arith and trymt): both operands converted to
// numbers, or the other operand's metamethod, or its error. The result goes
// to stack slot `result`.
Status arith_slow(State* state, const Instruction* pc, const Operand& p1, const Operand& p2,
                  int event, std::uint32_t result) {
    protect(state, pc);
    Heap& heap = state->heap();
    const auto e = static_cast<Event>(event);
    const bool string_library = !is_bitwise(event); // strings have arithmetic metamethods
    const auto call_tm = [&](Value tm) {
        Value r;
        if (call_metamethod(*state, tm, p1.value, p2.value, r) != Status::Ok) {
            return Status::Error;
        }
        state->set(result, r);
        return Status::Ok;
    };
    // The string library's metamethod: both as numbers, else the other
    // operand's metamethod (trymt).
    const auto string_arith = [&] {
        const ArithResult r = arith_event(event, p1.value, p2.value);
        if (r) {
            state->set(result, *r);
            return Status::Ok;
        }
        if (!p2.value.is_string()) {
            const Value tm = metamethod_of(heap, p2.value, e);
            if (!tm.is_nil()) {
                return call_tm(tm);
            }
        }
        return runtime_error(*state, r.error().message);
    };
    if (p1.value.is_string() && string_library) {
        return string_arith();
    }
    if (const Value tm = metamethod_of(heap, p1.value, e); !tm.is_nil()) {
        return call_tm(tm);
    }
    if (p2.value.is_string() && string_library) {
        return string_arith();
    }
    if (const Value tm = metamethod_of(heap, p2.value, e); !tm.is_nil()) {
        return call_tm(tm);
    }
    if (const ArithResult r = arith_event(event, p1.value, p2.value)) {
        state->set(result, *r); // numbers (a path the fast code left out)
        return Status::Ok;
    }
    return binary_error(state, p1, p2, event);
}

// ---- Comparisons ----

// luaG_ordererror, with __name (M3.7).
[[gnu::cold, gnu::noinline]] Status order_error(State* state, Value a, Value b) {
    const std::string t1{object_type_name(state->heap(), a)};
    const std::string t2{object_type_name(state->heap(), b)};
    if (t1 == t2) {
        return runtime_error(*state, std::format("attempt to compare two {} values", t1));
    }
    return runtime_error(*state, std::format("attempt to compare {} with {}", t1, t2));
}

// luaT_callorderTM: a < b or a <= b through __lt or __le (M3.5), with
// LUA_COMPAT_LT_LE's fallback of a <= b to not (b < a).
Status order_metamethod(State* state, Value a, Value b, bool less_or_equal, bool& cond) {
    const Heap& heap = state->heap();
    const Event e = less_or_equal ? Event::Le : Event::Lt;
    Value tm = metamethod_of(heap, a, e);
    if (tm.is_nil()) {
        tm = metamethod_of(heap, b, e);
    }
    Value r;
    if (!tm.is_nil()) {
        if (call_metamethod(*state, tm, a, b, r) != Status::Ok) {
            return Status::Error;
        }
        cond = r.is_truthy();
        return Status::Ok;
    }
    if (less_or_equal) {
        tm = metamethod_of(heap, b, Event::Lt);
        if (tm.is_nil()) {
            tm = metamethod_of(heap, a, Event::Lt);
        }
        if (!tm.is_nil()) {
            if (call_metamethod(*state, tm, b, a, r) != Status::Ok) {
                return Status::Error;
            }
            cond = r.is_falsy();
            return Status::Ok;
        }
    }
    return order_error(state, a, b);
}

// luaV_lessthan and luaV_lessequal for anything but two integers or two
// floats.
Status order_slow(State* state, const Instruction* pc, Value a, Value b, bool less_or_equal,
                  bool& cond) {
    protect(state, pc);
    if ((a.is_number() && b.is_number()) || (a.is_string() && b.is_string())) {
        const CompareResult r = less_or_equal ? less_equal(a, b) : less_than(a, b);
        cond = r.value_or(false); // numbers and strings always compare
        return Status::Ok;
    }
    return order_metamethod(state, a, b, less_or_equal, cond);
}

// luaV_equalobj: == with __eq for two different tables (M3.4).
Status equal_slow(State* state, const Instruction* pc, Value a, Value b, bool& cond) {
    // Tables and userdata: two different objects of the same type.
    if (a.tag() != b.tag() || (!a.is_table() && !a.is_userdata()) || a.payload() == b.payload()) {
        cond = equals(a, b);
        return Status::Ok;
    }
    protect(state, pc);
    const Heap& heap = state->heap();
    Value tm = metamethod_in(heap, metatable_of(heap, a), Event::Eq);
    if (tm.is_nil()) {
        tm = metamethod_in(heap, metatable_of(heap, b), Event::Eq);
    }
    if (tm.is_nil()) {
        cond = false;
        return Status::Ok;
    }
    Value r;
    if (call_metamethod(*state, tm, a, b, r) != Status::Ok) {
        return Status::Error;
    }
    cond = r.is_truthy();
    return Status::Ok;
}

// ---- Concatenation ----

bool joinable(Value v) {
    return v.is_string() || v.is_number();
}

// luaV_concat with metamethods: the values in slots first..first+total-1,
// joined from the right, a pair at a time where a metamethod is needed
// (M3.3). The result goes to `first`.
[[gnu::noinline]] Status concat_slow(State* state, const Instruction* pc, std::uint32_t first,
                                     int total) {
    state->ci->saved_pc = pc;
    std::uint32_t top = first + u32(total);
    state->top = top;
    std::vector<Value> run;
    while (total > 1) {
        const Value a = state->get(top - 2);
        const Value b = state->get(top - 1);
        int n = 2;
        if (joinable(a) && joinable(b)) {
            while (n < total && joinable(state->get(top - u32(n) - 1))) {
                ++n;
            }
            run.clear();
            for (std::uint32_t slot = top - u32(n); slot < top; ++slot) {
                run.push_back(state->get(slot));
            }
            const auto joined = concatenate(state->heap(), run);
            if (!joined) {
                return runtime_error(*state, joined.error().message);
            }
            state->set(top - u32(n), *joined);
        } else {
            Value tm = metamethod_of(state->heap(), a, Event::Concat);
            if (tm.is_nil()) {
                tm = metamethod_of(state->heap(), b, Event::Concat);
            }
            if (tm.is_nil()) {
                // luaG_concaterror: the left operand, unless it is joinable.
                const std::uint32_t culprit = joinable(a) ? top - 1 : top - 2;
                return type_error(
                    state, state->get(culprit), "concatenate",
                    register_info_here(state, static_cast<int>(culprit - state->ci->func - 1)));
            }
            Value r;
            if (call_metamethod(*state, tm, a, b, r) != Status::Ok) {
                return Status::Error;
            }
            state->set(top - 2, r);
        }
        total -= n - 1;
        top -= u32(n) - 1;
        state->top = top;
    }
    return Status::Ok;
}

[[gnu::noinline]] Status concat_registers(State* state, const Instruction* pc, Tag* tags,
                                          std::uint64_t* vals, int a, int n) {
    // R[A] .. ... .. R[A+n-1]: when every operand is a string or a number,
    // the whole chain at once; otherwise luaV_concat's way.
    constexpr std::size_t on_stack = 8;
    std::array<Value, on_stack> small{};
    std::vector<Value> large;
    std::span<Value> values;
    if (std::cmp_less_equal(n, on_stack)) {
        values = std::span{small}.first(static_cast<std::size_t>(n));
    } else {
        large.resize(static_cast<std::size_t>(n));
        values = large;
    }
    bool all_joinable = true;
    for (int j = 0; j < n; ++j) {
        values[static_cast<std::size_t>(j)] = register_value(tags, vals, a + j);
        all_joinable = all_joinable && joinable(values[static_cast<std::size_t>(j)]);
    }
    if (all_joinable) [[likely]] {
        const auto r = concatenate(state->heap(), values);
        if (r) [[likely]] {
            set_register(tags, vals, a, *r);
            return Status::Ok;
        }
        return raise(state, pc, r.error().message);
    }
    return concat_slow(state, pc, base_index(state, tags) + u32(a), n);
}

// ---- Tables ----

// K[index], a string constant (the compiler puts only short strings there
// for the instructions that use this).
const String* string_constant(const Value* k, int index) {
    return std::bit_cast<const String*>(k[index].payload());
}

// RK(C): a constant when the k bit is set, a register otherwise.
Value rk_value(const Tag* tags, const std::uint64_t* vals, const Value* k, Instruction i) {
    return get_k(i) ? k[get_c(i)] : register_value(tags, vals, get_c(i));
}

// #v with __len (luaV_objlen, M3.6) into slot `result`.
Status length_slow(State* state, const Instruction* pc, Value v, int reg, std::uint32_t result) {
    protect(state, pc);
    const Heap& heap = state->heap();
    Value tm;
    if (v.is_table()) {
        tm = metamethod_in(heap, table_of(v)->metatable, Event::Len);
        if (tm.is_nil()) {
            state->set(result, Value::integer(static_cast<std::int64_t>(table_of(v)->length())));
            return Status::Ok;
        }
    } else if (v.is_string()) {
        state->set(result, Value::integer(static_cast<std::int64_t>(v.as_string()->length())));
        return Status::Ok;
    } else {
        tm = metamethod_of(heap, v, Event::Len);
        if (tm.is_nil()) {
            return type_error(state, v, "get length of", register_info_here(state, reg));
        }
    }
    Value r;
    if (call_metamethod(*state, tm, v, v, r) != Status::Ok) {
        return Status::Error;
    }
    state->set(result, r);
    return Status::Ok;
}

// ---- Numeric for (forprep and forlimit) ----

enum class ForPrep : std::uint8_t { Run, Skip, Error };

// luaV_tointeger: a number or a numeric string, as an integer rounded with
// `rounding`.
bool to_integer_coerced(Value v, Rounding rounding, std::int64_t& out) {
    if (v.is_string()) {
        const auto number = string_to_number(v.as_string()->view());
        if (!number) {
            return false;
        }
        v = *number;
    }
    const auto i = to_integer(v, rounding);
    if (!i) {
        return false;
    }
    out = *i;
    return true;
}

// tonumber: a number or a numeric string, as a float.
bool to_float_coerced(Value v, double& out) {
    if (v.is_string()) {
        const auto number = string_to_number(v.as_string()->view());
        if (!number) {
            return false;
        }
        v = *number;
    }
    const auto f = to_float(v);
    if (!f) {
        return false;
    }
    out = *f;
    return true;
}

[[gnu::cold]] ForPrep for_error(State* state, const Instruction* pc, Value v,
                                std::string_view what) {
    // luaG_forerror
    raise(state, pc,
          std::format("bad 'for' {} (number expected, got {})", what,
                      object_type_name(state->heap(), v)));
    return ForPrep::Error;
}

[[gnu::noinline]] ForPrep for_prepare(State* state, const Instruction* pc, Tag* tags,
                                      std::uint64_t* vals, int a) {
    const Value init = register_value(tags, vals, a);
    const Value limit = register_value(tags, vals, a + 1);
    const Value step = register_value(tags, vals, a + 2);
    if (init.is_integer() && step.is_integer()) { // an integer loop (E5.3)
        const std::int64_t i0 = init.as_integer();
        const std::int64_t s = step.as_integer();
        if (s == 0) {
            raise(state, pc, "'for' step is zero");
            return ForPrep::Error;
        }
        set_integer(tags, vals, a + 3, i0);
        std::int64_t last = 0;
        if (!to_integer_coerced(limit, s < 0 ? Rounding::Ceil : Rounding::Floor, last)) {
            // forlimit: a float beyond the integers clips the loop (E5.4).
            double flimit = 0;
            if (!to_float_coerced(limit, flimit)) {
                return for_error(state, pc, limit, "limit");
            }
            if (0 < flimit) {
                if (s < 0) {
                    return ForPrep::Skip;
                }
                last = std::numeric_limits<std::int64_t>::max();
            } else {
                if (s > 0) {
                    return ForPrep::Skip;
                }
                last = std::numeric_limits<std::int64_t>::min();
            }
        }
        if (s > 0 ? i0 > last : i0 < last) {
            return ForPrep::Skip;
        }
        // The number of iterations left, computed once, so the loop never
        // overflows (E5.4).
        std::uint64_t count = 0;
        if (s > 0) {
            count = static_cast<std::uint64_t>(last) - static_cast<std::uint64_t>(i0);
            if (s != 1) {
                count /= static_cast<std::uint64_t>(s);
            }
        } else {
            count = static_cast<std::uint64_t>(i0) - static_cast<std::uint64_t>(last);
            count /= static_cast<std::uint64_t>(-(s + 1)) + 1U;
        }
        set_integer(tags, vals, a + 1, std::bit_cast<std::int64_t>(count));
        return ForPrep::Run;
    }
    // A float loop: all three become floats.
    double flimit = 0;
    double fstep = 0;
    double finit = 0;
    if (!to_float_coerced(limit, flimit)) {
        return for_error(state, pc, limit, "limit");
    }
    if (!to_float_coerced(step, fstep)) {
        return for_error(state, pc, step, "step");
    }
    if (!to_float_coerced(init, finit)) {
        return for_error(state, pc, init, "initial value");
    }
    if (fstep == 0) {
        raise(state, pc, "'for' step is zero");
        return ForPrep::Error;
    }
    if (0 < fstep ? flimit < finit : finit < flimit) {
        return ForPrep::Skip;
    }
    set_float(tags, vals, a + 1, flimit);
    set_float(tags, vals, a + 2, fstep);
    set_float(tags, vals, a, finit);
    set_float(tags, vals, a + 3, finit);
    return ForPrep::Run;
}

// ---- Calls (ldo.c) ----

[[gnu::noinline]] void collect_step(State* state) { // NOLINT(misc-const-correctness)
    state->heap().step();
}

// checkstackGCp: before growing the stack, a collection step if one is due
// (growing uses memory). False on "stack overflow" (E6.7), with the bare
// message in error_value (stack_error adds the position).
[[gnu::noinline]] bool grow_for_call(State* state, std::uint32_t n) {
    if (state->heap().wants_step()) {
        collect_step(state);
    }
    return state->grow_stack(n);
}

// moveresults: `nres` results just below state->top go to `res`, adjusted to
// `wanted` values (E3.1), and state->top goes right after them.
void move_results(State* state, std::uint32_t res, int nres, int wanted) {
    switch (wanted) {
    case 0: state->top = res; return;
    case 1:
        if (nres == 0) {
            state->stack_tags[res] = Tag::Nil;
            state->stack_values[res] = 0;
        } else {
            const std::uint32_t first = state->top - static_cast<std::uint32_t>(nres);
            state->stack_tags[res] = state->stack_tags[first];
            state->stack_values[res] = state->stack_values[first];
        }
        state->top = res + 1;
        return;
    case all_results: wanted = nres; break;
    default: break;
    }
    const std::uint32_t first = state->top - static_cast<std::uint32_t>(nres);
    const int moved = std::min(nres, wanted);
    for (int j = 0; j < moved; ++j) {
        state->stack_tags[res + u32(j)] = state->stack_tags[first + u32(j)];
        state->stack_values[res + u32(j)] = state->stack_values[first + u32(j)];
    }
    for (int j = moved; j < wanted; ++j) {
        state->stack_tags[res + u32(j)] = Tag::Nil;
        state->stack_values[res + u32(j)] = 0;
    }
    state->top = res + static_cast<std::uint32_t>(wanted);
}

// precallC with luaD_poscall: runs the C++ function in slot `func` with the
// arguments up to `top`, and leaves `wanted` of its results at `func`. A C++
// function gets min_stack free slots, and its own frame. On an error the
// frame stays, for the message handler and tracebacks.
Status call_c(State* state, std::uint32_t func, std::uint32_t top, int wanted) {
    state->top = top;
    if (state->stack_last - top <= min_stack && !grow_for_call(state, min_stack)) {
        return stack_error(state);
    }
    CallInfo* ci = state->next_call_info();
    ci->func = func;
    ci->top = state->top + min_stack;
    ci->wanted = wanted;
    ci->fresh = false;
    ci->tail_call = false;
    ci->finalizer = false;
    ci->saved_pc = nullptr;
    ci->constants = nullptr;
    const CFunction function =
        state->stack_tags[func] == Tag::CClosure
            ? std::bit_cast<const CClosure*>(state->stack_values[func])->function
            : std::bit_cast<CFunction>(state->stack_values[func]);
    const int n = function(*state);
    if (n < 0) [[unlikely]] {
        return Status::Error;
    }
    move_results(state, func, n, wanted);
    state->ci = ci->previous;
    return Status::Ok;
}

// tryfuncTM: until slot `func` holds a function, replaces it by its __call
// metamethod and shifts the arguments up, the value becoming the first
// (M4.1). `top` is one past the arguments and moves with them.
bool is_function_tag(Tag tag) {
    return tag == Tag::LuaClosure || tag == Tag::CFunction || tag == Tag::CClosure;
}

[[gnu::noinline]] Status resolve_callable(State* state, std::uint32_t func, std::uint32_t& top) {
    while (!is_function_tag(state->stack_tags[func])) {
        const Value value = state->get(func);
        state->top = top;
        if (state->stack_last - top <= 1 && !grow_for_call(state, 1)) {
            return stack_error(state);
        }
        const Value tm = metamethod_of(state->heap(), value, Event::Call);
        if (tm.is_nil()) {
            return call_error(state, value);
        }
        for (std::uint32_t p = top; p > func; --p) {
            state->stack_tags[p] = state->stack_tags[p - 1];
            state->stack_values[p] = state->stack_values[p - 1];
        }
        ++top;
        state->set(func, tm);
    }
    state->top = top;
    return Status::Ok;
}

// Makes sure the Lua function in `func` has its frame's room and a free
// frame (checkstackGCp, luaE_extendCI), for the cold call paths.
bool room_for_lua(State* state, std::uint32_t func, std::uint32_t top) {
    const Proto* p = std::bit_cast<const LuaClosure*>(state->stack_values[func])->proto;
    state->top = top;
    const std::uint32_t needed = p->max_stack_size + 1U;
    if (state->stack_last - top < needed && !grow_for_call(state, needed)) {
        return false;
    }
    state->reserve_call_info();
    return true;
}

// ---- To-be-closed variables (lfunc.c) ----

// luaF_newtbcupval: `slot` becomes a to-be-closed variable, if it holds
// something to close.
Status new_to_be_closed(State* state, const Instruction* pc, std::uint32_t slot, int reg) {
    const Value v = state->get(slot);
    if (v.is_falsy()) {
        return Status::Ok; // nil and false need no closing (M6.1)
    }
    if (metamethod_of(state->heap(), v, Event::Close).is_nil()) {
        return not_closable(state, pc, reg);
    }
    state->to_be_closed.push_back(slot);
    return Status::Ok;
}

// luaF_close: closes the upvalues at `level` and above, then calls the
// __close metamethod of each to-be-closed variable there, the last one
// first (M6.2). When `unwinding`, each call gets the error object, and goes
// just above its variable (prepcallclosemth), since nothing above is alive.
Status close_level(State* state, std::uint32_t level, bool unwinding) {
    state->close_upvalues(level);
    while (!state->to_be_closed.empty() && state->to_be_closed.back() >= level) {
        const std::uint32_t slot = state->to_be_closed.back();
        state->to_be_closed.pop_back();
        const Value object = state->get(slot);
        Value error;
        if (unwinding) {
            error = state->error_value;
            state->set(slot + 1, error);
            state->top = slot + 2;
        }
        const std::uint32_t func = state->top; // EXTRA_STACK has room
        state->set(func, metamethod_of(state->heap(), object, Event::Close));
        state->set(func + 1, object);
        state->set(func + 2, error);
        state->top = func + 3;
        if (call(*state, func, 0) != Status::Ok) {
            return Status::Error;
        }
    }
    return Status::Ok;
}

// ---- Error handling (ldo.c, ldebug.c) ----

// luaG_errormsg's call of the message handler, where the error happened:
// its result replaces the error object. If the handler fails, Lua calls it
// again for that error from inside the failed call, so a handler that always
// fails goes deeper until "C stack overflow", and from 220 levels on the
// error is "error in error handling" (R2.4, R3.2).
void run_message_handler(State* state, Value handler) {
    constexpr int error_in_error_limit = max_c_calls / 10 * 11;
    if (state->c_calls + 1 >= error_in_error_limit) {
        state->set_error("error in error handling"); // luaD_errerr
        return;
    }
    std::uint32_t top = state->top;
    if (state->is_lua(state->ci)) {
        top = std::max(top, state->ci->top);
    }
    state->set(top, handler);
    state->set(top + 1, state->error_value);
    state->top = top + 2;
    if (call(*state, top, 1) == Status::Ok) {
        state->error_value = state->get(top);
        state->top = top;
        return;
    }
    // (call() left the count of C++ calls where the error was, as Lua's is.)
    run_message_handler(state, handler);
}

// luaD_closeprotected: closes everything at `level` and above; an error in
// a __close becomes the error, and the closing goes on (M6.3). Returns
// whether there was an error at the end.
bool close_protected(State* state, std::uint32_t level, Value handler, bool error) {
    CallInfo* const ci = state->ci;
    for (;;) {
        if (close_level(state, level, error) == Status::Ok) {
            return error;
        }
        if (!handler.is_nil()) {
            run_message_handler(state, handler);
        }
        state->ci = ci;
        error = true;
    }
}

// ---- Dispatch ----

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#define LUA2_OPCODE_LIST(X)                                                                        \
    X(Move)                                                                                        \
    X(LoadI)                                                                                       \
    X(LoadF)                                                                                       \
    X(LoadK)                                                                                       \
    X(LoadKX)                                                                                      \
    X(LoadFalse)                                                                                   \
    X(LFalseSkip)                                                                                  \
    X(LoadTrue)                                                                                    \
    X(LoadNil)                                                                                     \
    X(GetUpval)                                                                                    \
    X(SetUpval)                                                                                    \
    X(GetTabUp)                                                                                    \
    X(GetTable)                                                                                    \
    X(GetI)                                                                                        \
    X(GetField)                                                                                    \
    X(SetTabUp)                                                                                    \
    X(SetTable)                                                                                    \
    X(SetI)                                                                                        \
    X(SetField)                                                                                    \
    X(NewTable)                                                                                    \
    X(Self)                                                                                        \
    X(AddI)                                                                                        \
    X(AddK)                                                                                        \
    X(SubK)                                                                                        \
    X(MulK)                                                                                        \
    X(ModK)                                                                                        \
    X(PowK)                                                                                        \
    X(DivK)                                                                                        \
    X(IDivK)                                                                                       \
    X(BAndK)                                                                                       \
    X(BOrK)                                                                                        \
    X(BXorK)                                                                                       \
    X(ShrI)                                                                                        \
    X(ShlI)                                                                                        \
    X(Add)                                                                                         \
    X(Sub)                                                                                         \
    X(Mul)                                                                                         \
    X(Mod)                                                                                         \
    X(Pow)                                                                                         \
    X(Div)                                                                                         \
    X(IDiv)                                                                                        \
    X(BAnd)                                                                                        \
    X(BOr)                                                                                         \
    X(BXor)                                                                                        \
    X(Shl)                                                                                         \
    X(Shr)                                                                                         \
    X(MmBin)                                                                                       \
    X(MmBinI)                                                                                      \
    X(MmBinK)                                                                                      \
    X(Unm)                                                                                         \
    X(BNot)                                                                                        \
    X(Not)                                                                                         \
    X(Len)                                                                                         \
    X(Concat)                                                                                      \
    X(Close)                                                                                       \
    X(Tbc)                                                                                         \
    X(Jmp)                                                                                         \
    X(Eq)                                                                                          \
    X(Lt)                                                                                          \
    X(Le)                                                                                          \
    X(EqK)                                                                                         \
    X(EqI)                                                                                         \
    X(LtI)                                                                                         \
    X(LeI)                                                                                         \
    X(GtI)                                                                                         \
    X(GeI)                                                                                         \
    X(Test)                                                                                        \
    X(TestSet)                                                                                     \
    X(Call)                                                                                        \
    X(TailCall)                                                                                    \
    X(Return)                                                                                      \
    X(Return0)                                                                                     \
    X(Return1)                                                                                     \
    X(ForLoop)                                                                                     \
    X(ForPrep)                                                                                     \
    X(TForPrep)                                                                                    \
    X(TForCall)                                                                                    \
    X(TForLoop)                                                                                    \
    X(SetList)                                                                                     \
    X(Closure)                                                                                     \
    X(VarArg)                                                                                      \
    X(VarArgPrep)                                                                                  \
    X(ExtraArg)

// The continuations of handlers.inc that hot handlers jump to
// (VM_COLD_HANDLER for the rare ones, VM_WARM_HANDLER for paths a program
// may take all the time, such as method lookups through __index: those stay
// with the hot code instead of the program's cold section).
#define LUA2_WARM_LIST(X)                                                                          \
    X(CallOther)                                                                                   \
    X(ForIterator)                                                                                 \
    X(Index)                                                                                       \
    X(NewIndex)
#define LUA2_COLD_LIST(X)                                                                          \
    X(GrowAndRetry)                                                                                \
    X(SetUpvalBarrier)                                                                             \
    X(TableBarrier)                                                                                \
    X(DivideByZero)                                                                                \
    X(ModuloByZero)                                                                                \
    X(FloatArith)                                                                                  \
    X(Unary)                                                                                       \
    X(Length)                                                                                      \
    X(Compare)                                                                                     \
    X(ReturnClose)

#if LUA2_TAIL_CALLS

// Every instruction is a function taking the VM's state; preserve_none
// passes all of it in registers and saves none, so jumping from one
// instruction to the next is a plain indirect jump. No handler may turn a
// loop into a call to memset (LOADNIL): calls need a stack frame.
#define LUA2_HANDLER_CC __attribute__((preserve_none))
#define LUA2_HANDLER_DEFINITION __attribute__((preserve_none, no_builtin("memset")))
#define VM_PARAMS                                                                                  \
    [[maybe_unused]] State *state, [[maybe_unused]] const Instruction *pc,                         \
        [[maybe_unused]] Tag *tags, [[maybe_unused]] std::uint64_t *vals,                          \
        [[maybe_unused]] const Value *k, [[maybe_unused]] Instruction i

using Handler = LUA2_HANDLER_CC Status (*)(State*, const Instruction*, Tag*, std::uint64_t*,
                                           const Value*, Instruction);

#define LUA2_DECLARE_HANDLER(name) LUA2_HANDLER_CC Status op_##name(VM_PARAMS);
LUA2_OPCODE_LIST(LUA2_DECLARE_HANDLER)
#undef LUA2_DECLARE_HANDLER
#define LUA2_DECLARE_COLD(name) [[gnu::cold]] LUA2_HANDLER_CC Status cold_##name(VM_PARAMS);
LUA2_COLD_LIST(LUA2_DECLARE_COLD)
#undef LUA2_DECLARE_COLD
#define LUA2_DECLARE_WARM(name) LUA2_HANDLER_CC Status cold_##name(VM_PARAMS);
LUA2_WARM_LIST(LUA2_DECLARE_WARM)
#undef LUA2_DECLARE_WARM

LUA2_HANDLER_CC Status op_invalid(VM_PARAMS) {
    return raise(state, pc, "invalid instruction");
}

// The opcode is 7 bits: 128 entries, the unused ones invalid.
constexpr std::array<Handler, 128> handlers = [] {
    std::array<Handler, 128> table{};
    table.fill(&op_invalid);
#define LUA2_TABLE_ENTRY(name) table[static_cast<std::size_t>(OpCode::name)] = &op_##name;
    LUA2_OPCODE_LIST(LUA2_TABLE_ENTRY)
#undef LUA2_TABLE_ENTRY
    return table;
}();

#define VM_HANDLER(name) LUA2_HANDLER_DEFINITION Status op_##name(VM_PARAMS)
#define VM_COLD_HANDLER(name)                                                                      \
    [[gnu::noinline, gnu::cold]] LUA2_HANDLER_DEFINITION Status cold_##name(VM_PARAMS)
#define VM_WARM_HANDLER(name)                                                                      \
    [[gnu::noinline]] LUA2_HANDLER_DEFINITION Status cold_##name(VM_PARAMS)
#define VM_COLD(name) [[clang::musttail]] return cold_##name(state, pc, tags, vals, k, i)
#define VM_NEXT()                                                                                  \
    do {                                                                                           \
        const Instruction next_ = *pc;                                                             \
        [[clang::musttail]] return handlers[static_cast<std::size_t>(get_op(next_))](              \
            state, pc + 1, tags, vals, k, next_);                                                  \
    } while (false)
#define VM_RETRY()                                                                                 \
    [[clang::musttail]] return handlers[static_cast<std::size_t>(get_op(i))](state, pc, tags,      \
                                                                             vals, k, i)

#include "handlers.inc"

#undef VM_HANDLER
#undef VM_COLD_HANDLER
#undef VM_WARM_HANDLER
#undef VM_COLD
#undef VM_NEXT
#undef VM_RETRY

// Runs the Lua function of state->ci until it returns.
Status execute(State* state) { // NOLINT(misc-const-correctness): the VM changes it
    const CallInfo* ci = state->ci;
    Tag* tags = state->stack_tags + ci->func + 1; // NOLINT(misc-const-correctness): the VM writes
    std::uint64_t* vals = state->stack_values + ci->func + 1;
    const Value* k = closure_of(vals)->proto->constants.data();
    const Instruction* pc = ci->saved_pc;
    const Instruction i = *pc;
    return handlers[static_cast<std::size_t>(get_op(i))](state, pc + 1, tags, vals, k, i);
}

#else // a switch in a loop

Status execute(State* state) {
    const CallInfo* entry = state->ci;
    Tag* tags = state->stack_tags + entry->func + 1;
    std::uint64_t* vals = state->stack_values + entry->func + 1;
    const Value* k = closure_of(vals)->proto->constants.data();
    const Instruction* pc = entry->saved_pc;
    for (;;) {
    dispatch:
        const Instruction i = *pc++;
        switch (get_op(i)) {
#define VM_HANDLER(name) case OpCode::name:
#define VM_COLD_HANDLER(name) cold_##name:
#define VM_WARM_HANDLER(name) cold_##name:
#define VM_COLD(name) goto cold_##name
#define VM_NEXT() goto dispatch
#define VM_RETRY()                                                                                 \
    --pc;                                                                                          \
    goto dispatch
#include "handlers.inc"
#undef VM_HANDLER
#undef VM_COLD_HANDLER
#undef VM_WARM_HANDLER
#undef VM_COLD
#undef VM_NEXT
#undef VM_RETRY
        default: return raise(state, pc, "invalid instruction");
        }
    }
}

#endif

#undef LUA2_OPCODE_LIST
#undef LUA2_COLD_LIST
#undef LUA2_WARM_LIST

// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace

// ---- The interface (execute.hpp) ----

Status runtime_error(State& thread, std::string_view message) {
    // luaG_runerror and luaG_addinfo.
    const CallInfo* ci = thread.ci;
    if (thread.is_lua(ci)) {
        const Proto& p = proto_of(&thread, ci);
        thread.set_error(std::format("{}:{}: {}", chunk_id(p.source->view()),
                                     line_of(p, frame_pc(&thread, ci)), message));
    } else {
        thread.set_error(message);
    }
    return Status::Error;
}

Status call_metamethod(State& thread, Value f, Value p1, Value p2, Value& result) {
    // luaT_callTMres
    const std::uint32_t func = thread.top; // EXTRA_STACK has room for three
    thread.set(func, f);
    thread.set(func + 1, p1);
    thread.set(func + 2, p2);
    thread.top = func + 3;
    if (call(thread, func, 1) != Status::Ok) {
        return Status::Error;
    }
    result = thread.get(func);
    thread.top = func;
    return Status::Ok;
}

Status index_value(State& thread, Value t, Value key, Value& result) {
    if (t.is_table()) {
        const Table::Slot slot = table_of(t)->get(key);
        if (!slot.empty()) {
            result = slot.value();
            return Status::Ok;
        }
    }
    const std::uint32_t slot = thread.top;
    thread.top = slot + 1;
    if (finish_get(&thread, t, key, slot, {}) != Status::Ok) {
        return Status::Error;
    }
    result = thread.get(slot);
    thread.top = slot;
    return Status::Ok;
}

Status assign_value(State& thread, Value t, Value key, Value value) {
    if (!thread.check_stack(5)) {
        return stack_error(&thread);
    }
    return finish_set(&thread, t, key, value, {});
}

Status compare_values(State& thread, Value a, Value b, bool less_or_equal, bool& result) {
    if ((a.is_number() && b.is_number()) || (a.is_string() && b.is_string())) {
        const CompareResult r = less_or_equal ? less_equal(a, b) : less_than(a, b);
        result = r.value_or(false);
        return Status::Ok;
    }
    return order_metamethod(&thread, a, b, less_or_equal, result);
}

Status equal_values(State& thread, Value a, Value b, bool& result) {
    if (a.tag() != b.tag() || (!a.is_table() && !a.is_userdata()) || a.payload() == b.payload()) {
        result = equals(a, b);
        return Status::Ok;
    }
    const Heap& heap = thread.heap();
    Value tm = metamethod_in(heap, metatable_of(heap, a), Event::Eq);
    if (tm.is_nil()) {
        tm = metamethod_in(heap, metatable_of(heap, b), Event::Eq);
    }
    if (tm.is_nil()) {
        result = false;
        return Status::Ok;
    }
    Value r;
    if (call_metamethod(thread, tm, a, b, r) != Status::Ok) {
        return Status::Error;
    }
    result = r.is_truthy();
    return Status::Ok;
}

Status length_value(State& thread, Value v, Value& result) {
    const Heap& heap = thread.heap();
    Value tm;
    if (v.is_table()) {
        tm = metamethod_in(heap, table_of(v)->metatable, Event::Len);
        if (tm.is_nil()) {
            result = Value::integer(static_cast<std::int64_t>(table_of(v)->length()));
            return Status::Ok;
        }
    } else if (v.is_string()) {
        result = Value::integer(static_cast<std::int64_t>(v.as_string()->length()));
        return Status::Ok;
    } else {
        tm = metamethod_of(heap, v, Event::Len);
        if (tm.is_nil()) {
            return type_error(&thread, v, "get length of", {});
        }
    }
    return call_metamethod(thread, tm, v, v, result);
}

Status call(State& thread, std::uint32_t func, int wanted) {
    // ccall and luaD_precall, from C++. On an error the count of C++ calls
    // stays where the error was, as Lua's does until its longjmp; the
    // protected call that catches the error restores it.
    State* state = &thread;
    if (++state->c_calls >= max_c_calls) [[unlikely]] {
        // luaE_checkcstack: room is left above the limit to handle the error.
        if (state->c_calls == max_c_calls) {
            return runtime_error(thread, "C stack overflow");
        }
        if (state->c_calls >= max_c_calls / 10 * 11) {
            thread.set_error("error in error handling");
            return Status::Error;
        }
    }
    std::uint32_t top = state->top;
    if (!is_function_tag(state->stack_tags[func]) &&
        resolve_callable(state, func, top) != Status::Ok) {
        return Status::Error;
    }
    if (state->stack_tags[func] != Tag::LuaClosure) {
        if (call_c(state, func, top, wanted) != Status::Ok) {
            return Status::Error;
        }
    } else {
        const Proto* p = std::bit_cast<const LuaClosure*>(state->stack_values[func])->proto;
        const std::uint32_t size = p->max_stack_size;
        if (state->stack_last - state->top <= size && !grow_for_call(state, size)) {
            return stack_error(state);
        }
        CallInfo* ci = state->next_call_info();
        ci->func = func;
        ci->top = func + 1 + size;
        ci->wanted = wanted;
        ci->fresh = true;
        ci->tail_call = false;
        ci->finalizer = false;
        ci->saved_pc = p->code.data();
        ci->constants = p->constants.data();
        for (std::uint32_t n = state->top - func - 1; n < p->parameter_count; ++n) {
            state->stack_tags[state->top] = Tag::Nil;
            state->stack_values[state->top] = 0;
            ++state->top;
        }
        if (execute(state) != Status::Ok) {
            return Status::Error;
        }
    }
    --state->c_calls;
    return Status::Ok;
}

Status protected_call(State& thread, std::uint32_t func, int wanted, Value handler) {
    // luaD_pcall: on an error, the message handler first (where the error
    // happened), then back to this frame, closing what the dropped frames
    // left open, and the error object in `func`.
    State* state = &thread;
    CallInfo* const ci = state->ci;
    const int c_calls = state->c_calls;
    if (call(thread, func, wanted) == Status::Ok) [[likely]] {
        return Status::Ok;
    }
    if (!handler.is_nil()) {
        run_message_handler(state, handler);
    }
    state->ci = ci;
    state->c_calls = c_calls;
    close_protected(state, func, handler, true);
    state->ci = ci;
    state->c_calls = c_calls;
    state->set(func, state->error_value);
    state->top = func + 1;
    state->shrink_stack(); // back to normal after a stack overflow
    return Status::Error;
}

Status close_variables(State& thread, std::uint32_t level) {
    return close_level(&thread, level, false);
}

void close_variables_protected(State& thread, std::uint32_t level) {
    const int c_calls = thread.c_calls;
    close_protected(&thread, level, Value::nil(), false);
    thread.c_calls = c_calls;
}

} // namespace lua2
