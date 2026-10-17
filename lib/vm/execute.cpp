// The interpreter: Lua 5.4's luaV_execute and the call machinery of ldo.c
// (docs/final_docs/vm.md). The instruction bodies are in handlers.inc,
// written once; this file puts them in one of two dispatch styles:
//
// - tail calls (the default with clang): each instruction is a function that
//   ends by jumping straight into the next instruction's function, with the
//   VM's state passed in registers ([[clang::musttail]] with the
//   preserve_none calling convention, measured fastest in
//   docs/learn/vm_gc.md);
// - a switch in a loop, for compilers without those (or CPPLUA_DISPATCH=switch).
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
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
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
#if defined(CPPLUA_DISPATCH_SWITCH) || !defined(__clang__)
#define CPPLUA_TAIL_CALLS 0
#else
#define CPPLUA_TAIL_CALLS 1
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace cpplua {

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
            if (const Status status = call_metamethod(*state, tm, t, key, value);
                status != Status::Ok) {
                return status;
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
    const Heap& heap = state->heap();
    const auto e = static_cast<Event>(event);
    const auto call_tm = [&](Value tm) {
        Value r;
        if (const Status status = call_metamethod(*state, tm, p1.value, p2.value, r);
            status != Status::Ok) {
            return status;
        }
        state->set(result, r);
        return Status::Ok;
    };
    // Strings convert through their metatable's arithmetic metamethods
    // (lstrlib's), like any other value's metamethods: a program that
    // changes or removes them changes string arithmetic, as in Lua.
    if (const Value tm = metamethod_of(heap, p1.value, e); !tm.is_nil()) {
        return call_tm(tm);
    }
    if (const Value tm = metamethod_of(heap, p2.value, e); !tm.is_nil()) {
        return call_tm(tm);
    }
    if (p1.value.is_number() && p2.value.is_number()) {
        // numbers (a path the fast code left out)
        if (const ArithResult r = arith_event(event, p1.value, p2.value)) {
            state->set(result, *r);
            return Status::Ok;
        }
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
        if (const Status status = call_metamethod(*state, tm, a, b, r); status != Status::Ok) {
            return status;
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
            CallInfo* ci = state->ci;
            ci->negate_compare = true; // for a yield in __lt (luaV_finishOp)
            const Status status = call_metamethod(*state, tm, b, a, r);
            if (status != Status::Yield) {
                ci->negate_compare = false;
            }
            if (status != Status::Ok) {
                return status;
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
    if (const Status status = call_metamethod(*state, tm, a, b, r); status != Status::Ok) {
        return status;
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
    // The joinable values of a run, gathered from the split stack: in a
    // buffer on the C++ stack for the usual short runs.
    std::array<Value, 32> small{};
    std::vector<Value> large;
    while (total > 1) {
        const Value a = state->get(top - 2);
        const Value b = state->get(top - 1);
        int n = 2;
        if (joinable(a) && joinable(b)) {
            while (n < total && joinable(state->get(top - u32(n) - 1))) {
                ++n;
            }
            std::span<Value> run;
            if (std::cmp_less_equal(n, small.size())) {
                run = std::span{small}.first(static_cast<std::size_t>(n));
            } else {
                large.resize(static_cast<std::size_t>(n));
                run = large;
            }
            for (std::size_t j = 0; j < run.size(); ++j) {
                run[j] = state->get(top - u32(n) + static_cast<std::uint32_t>(j));
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
            if (const Status status = call_metamethod(*state, tm, a, b, r); status != Status::Ok) {
                return status;
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
    if (const Status status = call_metamethod(*state, tm, v, v, r); status != Status::Ok) {
        return status;
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

// The hooks (below).
Status run_hook(State* state, HookEvent event, int line, int first_transfer, int transfer_count);
Status return_hook(State* state, const CallInfo* ci, std::uint32_t first, int count);

// A C++ exception, caught where an error stops (a protected call, a C++
// function's frame): the error it stands for goes in error_value, as if the
// code that threw had raised it. Out of memory (std::bad_alloc, or a size
// the library refuses with std::length_error) is "not enough memory", as
// Lua's LUA_ERRMEM, from the string made in advance; any other exception
// gives its what(). Called from inside a catch block.
[[gnu::cold]] Status exception_error(State* state) noexcept {
    const auto memory_error = [state] {
        state->set_error(Value::string(state->heap().memory_error_string()));
    };
    try {
        throw;
    } catch (const std::bad_alloc&) {
        memory_error();
    } catch (const std::length_error&) {
        memory_error();
    } catch (const std::exception& e) {
        try {
            state->set_error(std::string_view{e.what()});
        } catch (...) {
            memory_error();
        }
    } catch (...) {
        try {
            state->set_error(std::string_view{"C++ exception"});
        } catch (...) {
            memory_error();
        }
    }
    return Status::Error;
}

// Runs `body`, which returns a Status; a C++ exception from it is the error
// it stands for (exception_error). The VM's errors are return values, so
// nothing between the throw and here has cleanup to do that the protected
// call catching the error does not do anyway, as with Lua's longjmp.
template <typename Body>
Status guarded(State* state, Body&& body) {
    try {
        return std::forward<Body>(body)();
    } catch (...) {
        return exception_error(state);
    }
}

// precallC with luaD_poscall: runs the C++ function in slot `func` with the
// arguments up to `top`, and leaves `wanted` of its results at `func`. A C++
// function gets min_stack free slots, and its own frame. On an error the
// frame stays, for the message handler and tracebacks.
Status call_c(State* state, std::uint32_t func, std::uint32_t top, int wanted) {
    state->top = top;
    if (state->free_slots(top) <= min_stack && !grow_for_call(state, min_stack)) {
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
    if ((state->hook_mask & hook_call) != 0) [[unlikely]] {
        const int count = static_cast<int>(state->top - func) - 1;
        if (const Status status = run_hook(state, HookEvent::Call, -1, 1, count);
            status != Status::Ok) {
            return status;
        }
    }
    int n = 0;
    try {
        n = function(*state);
    } catch (...) {
        return exception_error(state); // the frame stays, as for an error
    }
    if (n < 0) [[unlikely]] {
        // An error, or a yield (C1): either way the frame stays.
        return n == c_function_yield ? Status::Yield : Status::Error;
    }
    if (state->hook_mask != 0) [[unlikely]] {
        if (const Status status = return_hook(state, ci, state->top - u32(n), n);
            status != Status::Ok) {
            return status;
        }
    }
    move_results(state, func, n, wanted);
    state->ci = ci->previous;
    // What the function allocated may call for a collection step: Lua's
    // API functions check after each allocation (luaC_checkGC), cpplua
    // here, with the results anchored below the top.
    if (state->heap().wants_step()) [[unlikely]] {
        collect_step(state);
    }
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
        if (state->free_slots(top) <= 1 && !grow_for_call(state, 1)) {
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
    if (state->free_slots(top) < needed && !grow_for_call(state, needed)) {
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
Status call_body(State& thread, std::uint32_t func, int wanted, int c_calls_increment);

// `yieldable`: a __close may yield even when called from C++ (a protected
// call closing after an error, which then finishes in its continuation).
// When `unwinding`, each __close gets `error` (an error inside one that a
// pcall there catches changes nothing for the others).
Status close_level(State* state, std::uint32_t level, bool unwinding, bool yieldable = false,
                   Value error = Value::nil()) {
    state->close_upvalues(level);
    while (!state->to_be_closed.empty() && state->to_be_closed.back() >= level) {
        const std::uint32_t slot = state->to_be_closed.back();
        state->to_be_closed.pop_back();
        const Value object = state->get(slot);
        if (unwinding) {
            state->set(slot + 1, error); // anchored there
            state->top = slot + 2;
        }
        const std::uint32_t func = state->top; // EXTRA_STACK has room
        state->set(func, metamethod_of(state->heap(), object, Event::Close));
        state->set(func + 1, object);
        state->set(func + 2, unwinding ? error : Value::nil());
        state->top = func + 3;
        const Status status = yieldable ? call_body(*state, func, 0, 1) : call(*state, func, 0);
        if (status != Status::Ok) {
            return status;
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
    if (state->error_in_handler) {
        return; // LUA_ERRERR goes to no handler
    }
    constexpr int error_in_error_limit = max_c_calls / 10 * 11;
    if (state->c_calls + 1 >= error_in_error_limit) {
        state->set_error_in_error_handling(); // luaD_errerr
        return;
    }
    std::uint32_t top = state->top;
    if (state->is_lua(state->ci)) {
        top = std::max(top, state->ci->top);
    }
    state->set(top, handler);
    state->set(top + 1, state->error_value);
    state->top = top + 2;
    if (guarded(state, [&] { return call_no_yield(*state, top, 1); }) == Status::Ok) {
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
    Value error_object = state->error_value; // what each __close gets
    for (;;) {
        if (guarded(state, [&] { return close_level(state, level, error, false, error_object); }) ==
            Status::Ok) {
            state->error_value = error_object;
            return error;
        }
        if (!handler.is_nil()) {
            run_message_handler(state, handler);
        }
        state->ci = ci;
        error = true;
        error_object = state->error_value; // the new error
    }
}

// The marks a frame keeps while running, for a frame given up after an
// error (Lua's callstatus is set afresh for every call).
void reset_frame_flags(CallInfo* ci) {
    ci->negate_compare = false;
    ci->yieldable_pcall = false;
    ci->pcall_error = false;
    ci->hooked = false;
    ci->transfer = false;
}

// finishpcallk after an error: the variables above the called function
// close, each __close getting the error, which waits in the function's
// slot meanwhile. A __close may yield (the frame's continuation finishes
// the job after the resume) or fail (its error replaces the first, and the
// closing goes on). Ends with Status::Error, the error at the function's
// slot and the top past it.
Status close_after_pcall_error(State* state, CallInfo* ci) {
    const std::uint32_t func = ci->pcall_func;
    ci->pcall_error = true;
    for (;;) {
        state->error_value = state->get(func);
        const Status status =
            guarded(state, [&] { return close_level(state, func, true, true, state->get(func)); });
        if (status == Status::Ok) {
            break;
        }
        if (status == Status::Yield) {
            return status;
        }
        if (!ci->pcall_handler.is_nil()) {
            run_message_handler(state, ci->pcall_handler);
        }
        for (CallInfo* dropped = state->ci; dropped != ci && dropped != nullptr;
             dropped = dropped->previous) {
            reset_frame_flags(dropped);
        }
        state->ci = ci;
        state->set(func, state->error_value);
    }
    state->error_value = state->get(func);
    state->top = func + 1;
    state->shrink_stack();
    ci->pcall_error = false;
    ci->yieldable_pcall = false;
    return Status::Error;
}

// The rest of a protected call that failed (luaD_pcall after the longjmp):
// the message handler where the error happened, then back to frame `ci`,
// closing what the dropped frames left open, the error object at `func`.
void recover(State* state, CallInfo* ci, std::uint32_t func, Value handler, int c_calls,
             int non_yieldable, bool allow_hook) {
    if (!handler.is_nil()) {
        run_message_handler(state, handler);
    }
    for (CallInfo* dropped = state->ci; dropped != ci && dropped != nullptr;
         dropped = dropped->previous) {
        reset_frame_flags(dropped);
    }
    ci->hooked = false;
    ci->transfer = false;
    state->allow_hook = allow_hook;
    state->pending_call_hook = 0;
    state->ci = ci;
    state->c_calls = c_calls;
    state->non_yieldable = non_yieldable;
    ++state->non_yieldable; // closing after an error cannot yield
    close_protected(state, func, handler, true);
    state->ci = ci;
    state->c_calls = c_calls;
    state->non_yieldable = non_yieldable;
    state->set(func, state->error_value);
    state->top = func + 1;
    state->shrink_stack(); // back to normal after a stack overflow
}

// ---- Debug hooks (ldo.c's luaD_hook, ldebug.c's luaG_traceexec; H1) ----

// luaD_hook with ldblib.c's hookf: the thread's hook function (in the
// registry's _HOOKKEY table, as debug.sethook leaves it) gets the event's
// name and the line, if any. Not inside another hook. `first_transfer` and
// `transfer_count` say which values the call or return passes.
Status run_hook(State* state, HookEvent event, int line, int first_transfer, int transfer_count) {
    if (!state->allow_hook) {
        return Status::Ok;
    }
    Heap& heap = state->heap();
    const Value hooks = heap.registry().get(Value::string(heap.hook_key_string())).value();
    if (!hooks.is_table()) {
        return Status::Ok;
    }
    const Value function = table_of(hooks)->get(Value::thread(state)).value();
    if (!function.is_function()) {
        return Status::Ok;
    }
    CallInfo* ci = state->ci;
    const std::uint32_t top = state->top;
    const std::uint32_t ci_top = ci->top;
    if (transfer_count != 0) {
        ci->transfer = true;
        ci->first_transfer = first_transfer;
        ci->transfer_count = transfer_count;
    }
    if (state->is_lua(ci) && state->top < ci->top) {
        state->top = ci->top; // protect every register
    }
    if (!state->check_stack(min_stack)) {
        return stack_error(state);
    }
    ci->top = std::max(ci->top, state->top + min_stack);
    state->allow_hook = false; // no hooks inside a hook
    ci->hooked = true;
    // As ldblib's hookf: the hook table, then the function called above it
    // (debug.getlocal sees that slot as a temporary of the hooked frame).
    state->set(state->top, hooks);
    const std::uint32_t func = state->top + 1;
    state->set(func, function);
    state->set(func + 1, Value::string(heap.hook_event_string(static_cast<std::size_t>(event))));
    state->set(func + 2, line >= 0 ? Value::integer(line) : Value::nil());
    state->top = func + 3;
    if (const Status status = call_no_yield(*state, func, 0); status != Status::Ok) {
        return status; // the protected call that catches it restores allow_hook
    }
    state->allow_hook = true;
    ci->top = ci_top;
    state->top = top;
    ci->hooked = false;
    ci->transfer = false;
    return Status::Ok;
}

// The instruction a Lua frame is at (pcRel of savedpc: the call it made),
// as the line hook's "old pc".
int next_pc_index(const State* state, const CallInfo* frame) {
    return static_cast<int>(frame->saved_pc - proto_of(state, frame).code.data()) - 1;
}

// rethook: the return hook of frame `ci`, its `count` results from slot
// `first`; then the line hook sees the caller as having already started its
// line.
Status return_hook(State* state, const CallInfo* ci, std::uint32_t first, int count) {
    if ((state->hook_mask & hook_return) != 0) {
        const auto first_transfer = static_cast<int>(first - ci->func);
        if (const Status status = run_hook(state, HookEvent::Return, -1, first_transfer, count);
            status != Status::Ok) {
            return status;
        }
    }
    const CallInfo* caller = ci->previous;
    if (caller != nullptr && state->is_lua(caller)) {
        state->old_pc = next_pc_index(state, caller);
    }
    return Status::Ok;
}

// Whether an instruction takes its values up to the top (isIT).
bool uses_top(Instruction i) {
    switch (get_op(i)) {
    case OpCode::Call:
    case OpCode::TailCall:
    case OpCode::Return:
    case OpCode::SetList: return get_b(i) == 0;
    case OpCode::VarArgPrep: return true;
    default: return false;
    }
}

// luaG_traceexec and luaG_tracecall, before instruction `i` of the running
// Lua frame (`pc` is past it): a call hook waiting for the function's first
// instruction, the count and line hooks, the return hook of a RETURN, and a
// call hook to come for a CALL.
[[gnu::noinline]] Status trace_instruction(State* state, const Instruction* pc, Instruction i) {
    CallInfo* ci = state->ci;
    if (state->interrupt_requested.load(std::memory_order_relaxed) && take_interrupt(*state)) {
        // lstop
        ci->saved_pc = pc;
        return runtime_error(*state, "interrupted!");
    }
    const Proto& p = proto_of(state, ci);
    const OpCode op = get_op(i);
    const int index = static_cast<int>(pc - p.code.data()) - 1;
    if (state->pending_call_hook != 0) {
        if (op == OpCode::VarArgPrep) {
            return Status::Ok; // a vararg function's hooks start after it
        }
        const HookEvent event =
            state->pending_call_hook == 2 ? HookEvent::TailCall : HookEvent::Call;
        state->pending_call_hook = 0;
        if ((state->hook_mask & hook_call) != 0) {
            ci->saved_pc = pc;
            if (const Status status = run_hook(state, event, -1, 1, p.parameter_count);
                status != Status::Ok) {
                return status;
            }
        }
        state->old_pc = index; // the first instruction is a new line
    }
    const std::uint8_t mask = state->hook_mask; // (the call hook may have changed it)
    if ((mask & hook_line) != 0 || (mask & hook_count) != 0) {
        ci->saved_pc = pc;
        const bool count_hook = (mask & hook_count) != 0 && --state->hook_count_left == 0;
        if (count_hook) {
            state->hook_count_left = state->base_hook_count;
        }
        if (count_hook || (mask & hook_line) != 0) {
            if (!uses_top(i)) {
                state->top = ci->top;
            }
            if (count_hook) {
                if (const Status status = run_hook(state, HookEvent::Count, -1, 0, 0);
                    status != Status::Ok) {
                    return status;
                }
            }
            if ((mask & hook_line) != 0) {
                const int old_pc = std::cmp_less(state->old_pc, p.code.size()) ? state->old_pc : 0;
                if (index <= old_pc || line_of(p, old_pc) != line_of(p, index)) {
                    if (const Status status =
                            run_hook(state, HookEvent::Line, line_of(p, index), 0, 0);
                        status != Status::Ok) {
                        return status;
                    }
                }
                state->old_pc = index;
            }
        }
    }
    const std::uint32_t base = ci->func + 1;
    switch (op) {
    case OpCode::Return0:
    case OpCode::Return1:
    case OpCode::Return:
        if (op == OpCode::Return && get_k(i)) {
            break; // after closing its variables (ReturnClose)
        }
        {
            ci->saved_pc = pc;
            const std::uint32_t first = base + u32(get_a(i));
            int count = op == OpCode::Return0 ? 0 : 1;
            if (op == OpCode::Return) {
                count = get_b(i) != 0 ? get_b(i) - 1 : static_cast<int>(state->top - first);
            }
            const std::uint32_t top = state->top;
            state->top = first + u32(count);
            const Status status = return_hook(state, ci, first, count);
            state->top = top;
            if (status != Status::Ok) {
                return status;
            }
        }
        break;
    case OpCode::Call:
    case OpCode::TailCall:
    case OpCode::TForCall:
        if (state->stack_tags[base + u32(get_a(i))] == Tag::LuaClosure) {
            state->pending_call_hook = op == OpCode::TailCall ? 2 : 1;
        }
        break;
    default: break;
    }
    return Status::Ok;
}

// ---- Dispatch ----

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#define CPPLUA_OPCODE_LIST(X)                                                                      \
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
#define CPPLUA_WARM_LIST(X)                                                                        \
    X(CallOther)                                                                                   \
    X(ForIterator)                                                                                 \
    X(Index)                                                                                       \
    X(NewIndex)
#define CPPLUA_COLD_LIST(X)                                                                        \
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

#if CPPLUA_TAIL_CALLS

// Every instruction is a function taking the VM's state; preserve_none
// passes all of it in registers and saves none, so jumping from one
// instruction to the next is a plain indirect jump. No handler may turn a
// loop into a call to memset (LOADNIL): calls need a stack frame.
#define CPPLUA_HANDLER_CC __attribute__((preserve_none))
#define CPPLUA_HANDLER_DEFINITION __attribute__((preserve_none, no_builtin("memset")))
#define VM_PARAMS                                                                                  \
    [[maybe_unused]] State *state, [[maybe_unused]] const Instruction *pc,                         \
        [[maybe_unused]] Tag *tags, [[maybe_unused]] std::uint64_t *vals,                          \
        [[maybe_unused]] const Value *k, [[maybe_unused]] Instruction i

using Handler = CPPLUA_HANDLER_CC Status (*)(State*, const Instruction*, Tag*, std::uint64_t*,
                                             const Value*, Instruction);

#define CPPLUA_DECLARE_HANDLER(name) CPPLUA_HANDLER_CC Status op_##name(VM_PARAMS);
CPPLUA_OPCODE_LIST(CPPLUA_DECLARE_HANDLER)
#undef CPPLUA_DECLARE_HANDLER
#define CPPLUA_DECLARE_COLD(name) [[gnu::cold]] CPPLUA_HANDLER_CC Status cold_##name(VM_PARAMS);
CPPLUA_COLD_LIST(CPPLUA_DECLARE_COLD)
#undef CPPLUA_DECLARE_COLD
#define CPPLUA_DECLARE_WARM(name) CPPLUA_HANDLER_CC Status cold_##name(VM_PARAMS);
CPPLUA_WARM_LIST(CPPLUA_DECLARE_WARM)
#undef CPPLUA_DECLARE_WARM

CPPLUA_HANDLER_CC Status op_invalid(VM_PARAMS) {
    return raise(state, pc, "invalid instruction");
}

// The opcode is 7 bits: 128 entries, the unused ones invalid.
constexpr std::array<Handler, 128> plain_handlers = [] {
    std::array<Handler, 128> table{};
    table.fill(&op_invalid);
#define CPPLUA_TABLE_ENTRY(name) table[static_cast<std::size_t>(OpCode::name)] = &op_##name;
    CPPLUA_OPCODE_LIST(CPPLUA_TABLE_ENTRY)
#undef CPPLUA_TABLE_ENTRY
    return table;
}();

CPPLUA_HANDLER_CC Status op_hooked(VM_PARAMS);

// The table a thread dispatches through while it has hooks or an interrupt
// is waiting (H2): op_hooked in every entry. Each thread has its own
// choice (State::dispatch), so hooks in one thread, or one interpreter,
// cost the others nothing.
constexpr std::array<Handler, 128> hooked_handlers = [] {
    std::array<Handler, 128> table{};
    table.fill(&op_hooked);
    return table;
}();

// State::dispatch is written by the thread itself, and by an interrupt
// from anywhere (a signal handler, another thread): read and written
// atomically (a relaxed load is a plain load).
const Handler* dispatch_of(State* state) {
    return static_cast<const Handler*>(
        std::atomic_ref<const void*>(state->dispatch).load(std::memory_order_relaxed));
}

const void* plain_dispatch() {
    return static_cast<const void*>(plain_handlers.data());
}

const void* hooked_dispatch() {
    return static_cast<const void*>(hooked_handlers.data());
}

#define VM_HANDLER(name) CPPLUA_HANDLER_DEFINITION Status op_##name(VM_PARAMS)
#define VM_COLD_HANDLER(name)                                                                      \
    [[gnu::noinline, gnu::cold]] CPPLUA_HANDLER_DEFINITION Status cold_##name(VM_PARAMS)
#define VM_WARM_HANDLER(name)                                                                      \
    [[gnu::noinline]] CPPLUA_HANDLER_DEFINITION Status cold_##name(VM_PARAMS)
#define VM_COLD(name) [[clang::musttail]] return cold_##name(state, pc, tags, vals, k, i)
#define VM_NEXT()                                                                                  \
    do {                                                                                           \
        const Instruction next_ = *pc;                                                             \
        [[clang::musttail]] return dispatch_of(state)[static_cast<std::size_t>(get_op(next_))](    \
            state, pc + 1, tags, vals, k, next_);                                                  \
    } while (false)
// (Again without the hooks: they saw this instruction already.)
#define VM_RETRY()                                                                                 \
    [[clang::musttail]] return plain_handlers[static_cast<std::size_t>(get_op(i))](                \
        state, pc, tags, vals, k, i)

#include "handlers.inc"

#undef VM_HANDLER
#undef VM_COLD_HANDLER
#undef VM_WARM_HANDLER
#undef VM_COLD
#undef VM_NEXT
#undef VM_RETRY

// Every instruction while hooks are on: the hooks of the running thread,
// if it has any, then the instruction itself.
CPPLUA_HANDLER_DEFINITION Status op_hooked(VM_PARAMS) {
    if (state->hook_mask != 0 || state->pending_call_hook != 0 ||
        state->interrupt_requested.load(std::memory_order_relaxed)) [[unlikely]] {
        const std::uint32_t base = base_index(state, tags);
        if (const Status status = trace_instruction(state, pc, i); status != Status::Ok) {
            return status;
        }
        tags = state->stack_tags + base;
        vals = state->stack_values + base;
    }
    [[clang::musttail]] return plain_handlers[static_cast<std::size_t>(get_op(i))](state, pc, tags,
                                                                                   vals, k, i);
}

// Runs the Lua function of state->ci until it returns.
Status execute(State* state) { // NOLINT(misc-const-correctness): the VM changes it
    const CallInfo* ci = state->ci;
    Tag* tags = state->stack_tags + ci->func + 1; // NOLINT(misc-const-correctness): the VM writes
    std::uint64_t* vals = state->stack_values + ci->func + 1;
    const Value* k = closure_of(vals)->proto->constants.data();
    const Instruction* pc = ci->saved_pc;
    const Instruction i = *pc;
    return dispatch_of(state)[static_cast<std::size_t>(get_op(i))](state, pc + 1, tags, vals, k, i);
}

#else // a switch in a loop

// Without a table of handlers, State::dispatch only says whether the
// thread has hooks (H2): non-null, and each instruction checks them.
constexpr int hooked_marker = 0;

const void* plain_dispatch() {
    return nullptr;
}

const void* hooked_dispatch() {
    return &hooked_marker;
}

Status execute(State* state) {
    const CallInfo* entry = state->ci;
    Tag* tags = state->stack_tags + entry->func + 1;
    std::uint64_t* vals = state->stack_values + entry->func + 1;
    const Value* k = closure_of(vals)->proto->constants.data();
    const Instruction* pc = entry->saved_pc;
    for (;;) {
    dispatch:
        if (std::atomic_ref<const void*>(state->dispatch).load(std::memory_order_relaxed) !=
                nullptr &&
            (state->hook_mask != 0 || state->pending_call_hook != 0 ||
             state->interrupt_requested.load(std::memory_order_relaxed))) [[unlikely]] {
            const std::uint32_t base = base_index(state, tags);
            if (const Status status = trace_instruction(state, pc + 1, *pc); status != Status::Ok) {
                return status;
            }
            tags = state->stack_tags + base;
            vals = state->stack_values + base;
        }
    dispatch_untraced:
        const Instruction i = *pc++;
        switch (get_op(i)) {
#define VM_HANDLER(name) case OpCode::name:
#define VM_COLD_HANDLER(name) cold_##name:
#define VM_WARM_HANDLER(name) cold_##name:
#define VM_COLD(name) goto cold_##name
#define VM_NEXT() goto dispatch
#define VM_RETRY()                                                                                 \
    --pc;                                                                                          \
    goto dispatch_untraced
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

#undef CPPLUA_OPCODE_LIST
#undef CPPLUA_COLD_LIST
#undef CPPLUA_WARM_LIST

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
    if (const Status status = call(thread, func, 1); status != Status::Ok) {
        return status;
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
    if (const Status status = finish_get(&thread, t, key, slot, {}); status != Status::Ok) {
        return status;
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
    if (const Status status = call_metamethod(thread, tm, a, b, r); status != Status::Ok) {
        return status;
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

namespace {

// ccall and luaD_precall, from C++, counting `c_calls_increment` C++ calls
// (0 when lua_resume has counted it). On an error or a yield the count of
// C++ calls stays where it was, as Lua's does until its longjmp; the
// protected call (or the resume) that catches it restores it.
Status call_body(State& thread, std::uint32_t func, int wanted, int c_calls_increment) {
    State* state = &thread;
    state->c_calls += c_calls_increment;
    if (state->c_calls >= max_c_calls) [[unlikely]] {
        // luaE_checkcstack: room is left above the limit to handle the error.
        if (state->c_calls == max_c_calls) {
            return runtime_error(thread, "C stack overflow");
        }
        if (state->c_calls >= max_c_calls / 10 * 11) {
            thread.set_error_in_error_handling();
            return Status::Error;
        }
    }
    std::uint32_t top = state->top;
    if (!is_function_tag(state->stack_tags[func]) &&
        resolve_callable(state, func, top) != Status::Ok) {
        return Status::Error;
    }
    if (state->stack_tags[func] != Tag::LuaClosure) {
        if (const Status status = call_c(state, func, top, wanted); status != Status::Ok) {
            return status;
        }
    } else {
        const Proto* p = std::bit_cast<const LuaClosure*>(state->stack_values[func])->proto;
        const std::uint32_t size = p->max_stack_size;
        if (state->free_slots(state->top) <= size && !grow_for_call(state, size)) {
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
        if (state->hook_mask != 0) [[unlikely]] {
            state->pending_call_hook = 1; // the call hook, at its first instruction
        }
        if (const Status status = execute(state); status != Status::Ok) {
            return status;
        }
    }
    state->c_calls -= c_calls_increment;
    return Status::Ok;
}

} // namespace

Status call(State& thread, std::uint32_t func, int wanted) {
    // luaT_callTM's rule: a call from Lua code (the VM, for a metamethod)
    // may yield; a call from a C++ function cannot (C2).
    if (thread.is_lua(thread.ci)) {
        return call_body(thread, func, wanted, 1);
    }
    return call_no_yield(thread, func, wanted);
}

Status call_no_yield(State& thread, std::uint32_t func, int wanted) {
    // luaD_callnoyield: the count stays up after an error, as c_calls does.
    ++thread.non_yieldable;
    const Status status = call_body(thread, func, wanted, 1);
    if (status == Status::Ok) {
        --thread.non_yieldable;
    }
    return status;
}

Status protected_call(State& thread, std::uint32_t func, int wanted, Value handler) {
    // luaD_pcall: on an error, the message handler first (where the error
    // happened), then back to this frame, closing what the dropped frames
    // left open, and the error object in `func`.
    State* state = &thread;
    CallInfo* const ci = state->ci;
    const int c_calls = state->c_calls;
    const int non_yieldable = state->non_yieldable;
    const bool allow_hook = state->allow_hook;
    if (guarded(state, [&] { return call_no_yield(thread, func, wanted); }) == Status::Ok)
        [[likely]] {
        return Status::Ok;
    }
    recover(state, ci, func, handler, c_calls, non_yieldable, allow_hook);
    return Status::Error;
}

Status protected_call_k(State& thread, std::uint32_t func, int wanted, Value handler,
                        Continuation continuation, std::intptr_t context) {
    // lua_pcallk: where the thread can yield, the call may yield; after the
    // resume, `continuation` finishes the C++ function (C3). Its frame keeps
    // what an error after the resume needs (CIST_YPCALL).
    if (!is_yieldable(thread)) {
        return protected_call(thread, func, wanted, handler);
    }
    State* state = &thread;
    CallInfo* const ci = state->ci;
    const int c_calls = state->c_calls;
    const int non_yieldable = state->non_yieldable;
    const bool allow_hook = state->allow_hook;
    ci->continuation = continuation;
    ci->context = context;
    ci->pcall_func = func;
    ci->pcall_handler = handler;
    ci->yieldable_pcall = true;
    const Status status = guarded(state, [&] { return call_body(thread, func, wanted, 1); });
    if (status == Status::Yield) {
        return status; // the frame keeps its continuation
    }
    if (status == Status::Ok) {
        ci->yieldable_pcall = false;
        return status;
    }
    // The error: the handler where it happened, then back to this frame.
    if (!handler.is_nil()) {
        run_message_handler(state, handler);
    }
    for (CallInfo* dropped = state->ci; dropped != ci && dropped != nullptr;
         dropped = dropped->previous) {
        reset_frame_flags(dropped);
    }
    state->ci = ci;
    state->c_calls = c_calls;
    state->non_yieldable = non_yieldable;
    state->allow_hook = allow_hook;
    state->pending_call_hook = 0;
    state->set(func, state->error_value);
    return close_after_pcall_error(state, ci);
}

Status call_k(State& thread, std::uint32_t func, int wanted, Continuation continuation,
              std::intptr_t context) {
    // lua_callk
    if (!is_yieldable(thread)) {
        return call_no_yield(thread, func, wanted);
    }
    thread.ci->continuation = continuation;
    thread.ci->context = context;
    return call_body(thread, func, wanted, 1);
}

// ---- Coroutines (ldo.c's resume, lvm.c's luaV_finishOp) ----

namespace {

// luaV_finishOp: the Lua frame on top was stopped by a yield in the middle
// of an instruction that called a metamethod; this does what the
// instruction had left to do with the metamethod's result (on the top of
// the stack), so that the frame can go on with the next one (C4).
Status finish_instruction(State* state) {
    CallInfo* ci = state->ci;
    const std::uint32_t base = ci->func + 1;
    const Instruction i = ci->saved_pc[-1]; // the interrupted instruction
    switch (get_op(i)) {
    case OpCode::MmBin:
    case OpCode::MmBinI:
    case OpCode::MmBinK:
        --state->top;
        state->set(base + u32(get_a(ci->saved_pc[-2])), state->get(state->top));
        break;
    case OpCode::Unm:
    case OpCode::BNot:
    case OpCode::Len:
    case OpCode::GetTabUp:
    case OpCode::GetTable:
    case OpCode::GetI:
    case OpCode::GetField:
    case OpCode::Self:
        --state->top;
        state->set(base + u32(get_a(i)), state->get(state->top));
        break;
    case OpCode::Lt:
    case OpCode::Le:
    case OpCode::LtI:
    case OpCode::LeI:
    case OpCode::GtI:
    case OpCode::GeI:
    case OpCode::Eq: { // (EQI and EQK cannot yield)
        bool result = state->get(state->top - 1).is_truthy();
        --state->top;
        if (ci->negate_compare) { // "<=" through "<"
            ci->negate_compare = false;
            result = !result;
        }
        if (result != get_k(i)) {
            ++ci->saved_pc; // the condition failed: skip the jump
        }
        break;
    }
    case OpCode::Concat: {
        const std::uint32_t top = state->top - 1; // the top when __concat was called
        const std::uint32_t first = base + u32(get_a(i));
        const int total = static_cast<int>(top - 1 - first); // still to join
        state->set(top - 2, state->get(top));                // the metamethod's result
        state->top = top - 1;
        return concat_slow(state, ci->saved_pc, first, total); // may yield again
    }
    case OpCode::Close: // a __close yielded: close the other variables
        --ci->saved_pc;
        break;
    case OpCode::Return: // the same, then return
        state->top = base + u32(get_a(i)) + u32(ci->return_count);
        --ci->saved_pc;
        break;
    default: // TFORCALL, CALL, TAILCALL, SETTABUP, SETTABLE, SETI, SETFIELD
        break;
    }
    return Status::Ok;
}

// luaD_poscall for C++ frame `ci` with n results on the top, with its
// return hook.
Status finish_c_frame(State* state, const CallInfo* ci, int n) {
    if (state->hook_mask != 0) [[unlikely]] {
        if (const Status status = return_hook(state, ci, state->top - u32(n), n);
            status != Status::Ok) {
            return status;
        }
    }
    move_results(state, ci->func, n, ci->wanted);
    state->ci = ci->previous;
    return Status::Ok;
}

// finishpcallk and finishCcall: the C++ frame on top made a call with a
// continuation, which ended after a resume; `error` when it ended with an
// error inside a protected call (precover).
Status finish_c_call(State* state, CallInfo* ci, bool error) {
    Status status = Status::Yield;
    if (ci->yieldable_pcall) {
        if (error) { // an error after the resume, which this call catches
            state->set(ci->pcall_func, state->error_value);
            ci->pcall_error = true;
        }
        if (ci->pcall_error) { // closing after the error, maybe not the first time
            status = close_after_pcall_error(state, ci);
            if (status == Status::Yield) {
                return status;
            }
        }
        ci->yieldable_pcall = false;
    }
    const int n = ci->continuation(*state, status, ci->context);
    if (n < 0) {
        return n == c_function_yield ? Status::Yield : Status::Error;
    }
    return finish_c_frame(state, ci, n);
}

// unroll: runs what is left of every frame of the thread, top down, until
// none is left, or a yield or an error stops it again. A Lua frame finishes
// its interrupted instruction then runs; execute() returns when the first
// frame entered from C++ returns, and the C++ frame below goes on in its
// continuation.
Status unroll(State* state, bool error) {
    while (state->ci != &state->base_ci) {
        CallInfo* ci = state->ci;
        Status status = Status::Ok;
        if (!state->is_lua(ci)) {
            status = finish_c_call(state, ci, error);
            error = false;
        } else {
            status = finish_instruction(state);
            if (status == Status::Ok) {
                status = execute(state);
            }
        }
        if (status != Status::Ok) {
            return status;
        }
    }
    return Status::Ok;
}

// findpcall: the innermost frame in a yieldable protected call.
CallInfo* find_pcall(const State* state) {
    for (CallInfo* ci = state->ci; ci != nullptr; ci = ci->previous) {
        if (ci->yieldable_pcall) {
            return ci;
        }
    }
    return nullptr;
}

// precover: after an error, while a protected call that yielded before is
// below it, that call catches the error (its message handler first, where
// the error happened) and the thread goes on from there.
Status recover_errors(State* state, Status status, int c_calls) {
    while (status == Status::Error) {
        CallInfo* ci = find_pcall(state);
        if (ci == nullptr) {
            break;
        }
        if (!ci->pcall_handler.is_nil()) {
            run_message_handler(state, ci->pcall_handler);
        }
        for (CallInfo* dropped = state->ci; dropped != ci; dropped = dropped->previous) {
            reset_frame_flags(dropped);
        }
        state->allow_hook = true;
        state->pending_call_hook = 0;
        state->ci = ci;
        state->c_calls = c_calls;
        state->non_yieldable = 0;
        status = guarded(state, [&] { return unroll(state, true); });
    }
    return status;
}

// The body of lua_resume, `nargs` values on the thread's top.
Status resume_body(State* co, int nargs) {
    if (co->status == ThreadStatus::Ok) { // starting the coroutine
        const std::uint32_t first = co->top - static_cast<std::uint32_t>(nargs);
        return call_body(*co, first - 1, all_results, 0);
    }
    // Resuming from a yield: the values are the results of the C++ function
    // that yielded (or of its continuation).
    co->status = ThreadStatus::Ok;
    const CallInfo* ci = co->ci;
    int n = nargs;
    if (ci->continuation != nullptr) { // lua_yieldk's continuation
        n = ci->continuation(*co, Status::Yield, ci->context);
        if (n < 0) {
            return n == c_function_yield ? Status::Yield : Status::Error;
        }
    }
    if (const Status status = finish_c_frame(co, ci, n); status != Status::Ok) {
        return status;
    }
    return unroll(co, false);
}

} // namespace

Status resume(State& co, const State& from, int nargs, int& results) {
    // lua_resume
    // resume_error: the message on the coroutine's top, the coroutine as it
    // was.
    const auto refuse = [&](std::string_view message) {
        co.top -= static_cast<std::uint32_t>(nargs);
        co.push(Value::string(co.heap().new_string(message)));
        return Status::Error;
    };
    if (co.status == ThreadStatus::Ok) { // may be starting a coroutine
        if (co.ci != &co.base_ci) {
            return refuse("cannot resume non-suspended coroutine");
        }
        if (co.top - (co.base_ci.func + 1) == static_cast<std::uint32_t>(nargs)) { // no function
            return refuse("cannot resume dead coroutine");
        }
    } else if (co.status != ThreadStatus::Yield) {
        return refuse("cannot resume dead coroutine");
    }
    co.c_calls = from.c_calls;
    if (co.c_calls >= max_c_calls) {
        return refuse("C stack overflow");
    }
    ++co.c_calls;
    co.non_yieldable = 0;
    const int c_calls = co.c_calls;
    Heap& heap = co.heap();
    State* const resumer = heap.running_thread();
    heap.set_running_thread(&co);
    Status status = guarded(&co, [&] { return resume_body(&co, nargs); });
    status = recover_errors(&co, status, c_calls);
    heap.set_running_thread(resumer);
    if (status == Status::Yield) {
        results = co.yielded;
    } else if (status == Status::Ok) {
        results = static_cast<int>(co.top - (co.ci->func + 1));
    } else {
        co.status = ThreadStatus::Dead; // the frames stay, for tracebacks
        co.push(co.error_value);        // luaD_seterrorobj, on its top
        results = 0;
    }
    return status;
}

int yield_values(State& thread, int n) {
    // lua_yield from a C++ function: its top n values go to the resume.
    if (!is_yieldable(thread)) {
        const bool main = &thread == &thread.heap().main_thread();
        runtime_error(thread, main ? "attempt to yield from outside a coroutine"
                                   : "attempt to yield across a C-call boundary");
        return c_function_error;
    }
    thread.status = ThreadStatus::Yield;
    thread.yielded = n;
    thread.ci->continuation = nullptr; // lua_yield has none
    return c_function_yield;
}

Status close_thread(State& co, const State& from) {
    // lua_closethread and luaE_resetthread: close what a suspended or dead
    // coroutine left open, its __close methods getting its error, if any.
    co.c_calls = from.c_calls;
    const bool had_error = co.status == ThreadStatus::Dead;
    for (CallInfo* frame = co.ci; frame != nullptr; frame = frame->previous) {
        reset_frame_flags(frame);
    }
    co.allow_hook = true;
    co.pending_call_hook = 0;
    co.ci = &co.base_ci;
    co.status = ThreadStatus::Ok;
    co.top = std::max<std::uint32_t>(co.top, 1);
    ++co.non_yieldable;
    Heap& heap = co.heap();
    State* const resumer = heap.running_thread();
    heap.set_running_thread(&co);
    const bool error = close_protected(&co, 1, Value::nil(), had_error);
    heap.set_running_thread(resumer);
    co.non_yieldable = 0;
    co.top = 1;
    co.to_be_closed.clear();
    co.shrink_call_infos();
    // Dead: its status is ok again, with nothing on its stack (as
    // luaE_resetthread leaves it once the error is taken).
    return error ? Status::Error : Status::Ok;
}

void update_dispatch(State& thread) {
    const bool hooked =
        thread.hook_mask != 0 || thread.interrupt_requested.load(std::memory_order_seq_cst);
    std::atomic_ref<const void*>(thread.dispatch)
        .store(hooked ? hooked_dispatch() : plain_dispatch(), std::memory_order_seq_cst);
    // An interrupt that came after the check above stored the hooked table
    // already, or comes after this: either way it is not lost.
    if (!hooked && thread.interrupt_requested.load(std::memory_order_seq_cst)) {
        std::atomic_ref<const void*>(thread.dispatch)
            .store(hooked_dispatch(), std::memory_order_seq_cst);
    }
}

void request_interrupt(State& thread) {
    // The flag first, then the table: the thread sees the flag whenever it
    // runs op_hooked.
    thread.interrupt_requested.store(true, std::memory_order_seq_cst);
    std::atomic_ref<const void*>(thread.dispatch)
        .store(hooked_dispatch(), std::memory_order_seq_cst);
}

bool take_interrupt(State& thread) {
    if (!thread.interrupt_requested.exchange(false, std::memory_order_seq_cst)) {
        return false;
    }
    update_dispatch(thread);
    return true;
}

void set_hook(State& thread, std::uint8_t mask, int count) {
    // lua_sethook
    if (mask == 0) {
        count = 0;
    }
    thread.hook_mask = mask;
    thread.base_hook_count = count;
    thread.hook_count_left = count;
    if (mask == 0) {
        thread.pending_call_hook = 0;
    }
    update_dispatch(thread);
}

Status close_variables(State& thread, std::uint32_t level) {
    return close_level(&thread, level, false);
}

void close_variables_protected(State& thread, std::uint32_t level) {
    const int c_calls = thread.c_calls;
    close_protected(&thread, level, Value::nil(), false);
    thread.c_calls = c_calls;
}

} // namespace cpplua
