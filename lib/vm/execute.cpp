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
// metamethod-like slow paths, stack growth, collection) is in the cold
// functions below, out of the way.

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

#include "value/arith.hpp"
#include "value/bitwise.hpp"
#include "value/compare.hpp"
#include "value/convert.hpp"
#include "value/number.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "vm/debug_info.hpp"
#include "vm/function.hpp"
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
    return (static_cast<unsigned>(a) << 3U) | static_cast<unsigned>(b);
}

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

const Proto& running_proto(const State* state) {
    return *std::bit_cast<const LuaClosure*>(state->stack_values[state->ci->func])->proto;
}

// The index of the instruction being run: pc already points past it.
int current_pc(const State* state, const Instruction* pc) {
    return static_cast<int>(pc - running_proto(state).code.data()) - 1;
}

// luaG_runerror: the message with the position of the instruction (E8.1).
[[gnu::cold, gnu::noinline]] Status raise(State* state, const Instruction* pc,
                                          std::string_view message) {
    const Proto& p = running_proto(state);
    const int line = line_of(p, current_pc(state, pc));
    std::string full = std::format("{}:{}: {}", chunk_id(p.source->view()), line, message);
    state->error_message = std::move(full);
    return Status::Error;
}

std::string register_info_here(const State* state, const Instruction* pc, int reg) {
    return register_info(running_proto(state), current_pc(state, pc), reg);
}

// typeerror: "attempt to <op> a <type> value<info>".
[[gnu::cold, gnu::noinline]] Status type_error(State* state, const Instruction* pc, Value value,
                                               std::string_view op, const std::string& info) {
    return raise(state, pc,
                 std::format("attempt to {} a {} value{}", op, type_name(value.type()), info));
}

[[gnu::cold, gnu::noinline]] Status call_error(State* state, const Instruction* pc, Value value) {
    // luaG_callerror: named after how the call was made (E6.3).
    return type_error(state, pc, value, "call",
                      called_info(running_proto(state), current_pc(state, pc)));
}

[[gnu::cold, gnu::noinline]] Status index_error_register(State* state, const Instruction* pc,
                                                         const Tag* tags, const std::uint64_t* vals,
                                                         int reg) {
    return type_error(state, pc, register_value(tags, vals, reg), "index",
                      register_info_here(state, pc, reg));
}

[[gnu::cold, gnu::noinline]] Status index_error_upvalue(State* state, const Instruction* pc,
                                                        const std::uint64_t* vals, int index) {
    // A global when _ENV is not a table: "attempt to index a nil value
    // (upvalue '_ENV')".
    const Value value = closure_of(vals)->upvalue(static_cast<std::size_t>(index))->value();
    return type_error(state, pc, value, "index", upvalue_info(running_proto(state), index));
}

[[gnu::cold, gnu::noinline]] Status not_closable(State* state, const Instruction* pc, int reg) {
    const String* name = local_name(running_proto(state), reg + 1, current_pc(state, pc));
    return raise(state, pc,
                 std::format("variable '{}' got a non-closable value",
                             name != nullptr ? name->view() : std::string_view{"?"}));
}

[[gnu::cold, gnu::noinline]] Status stack_overflow(State* state, const Instruction* pc) {
    const std::string message = std::move(state->error_message);
    return raise(state, pc, message);
}

// ---- Slow paths of arithmetic (luaT_trybinTM) ----

constexpr int no_register = -1;

// An operand and the register it is in, for messages.
struct Operand {
    Value value;
    int reg = no_register;
};

constexpr int tm_band = 13; // ltm.h's TMS order
constexpr int tm_shr = 17;
constexpr int tm_unm = 18;
constexpr int tm_bnot = 19;

ArithResult arith_event(int event, Value a, Value b) {
    switch (event) {
    case 6: return add(a, b);
    case 7: return subtract(a, b);
    case 8: return multiply(a, b);
    case 9: return modulo(a, b);
    case 10: return power(a, b);
    case 11: return divide(a, b);
    case 12: return floor_divide(a, b);
    case 13: return bit_and(a, b);
    case 14: return bit_or(a, b);
    case 15: return bit_xor(a, b);
    case 16: return shift_left(a, b);
    case 17: return shift_right(a, b);
    case tm_unm: return negate(a);
    case tm_bnot: return bit_not(a);
    default: break;
    }
    return std::unexpected(RuntimeError{"invalid metamethod event"});
}

std::string operand_info(const State* state, const Instruction* pc, const Operand& o) {
    return o.reg == no_register ? std::string{} : register_info_here(state, pc, o.reg);
}

// The error of a failed arithmetic or bitwise operation, as luaT_trybinTM
// raises it: strings have arithmetic metamethods (the string library's),
// whose own message stands; otherwise luaG_opinterror or luaG_tointerror
// name the wrong operand.
[[gnu::cold, gnu::noinline]] Status binary_error(State* state, const Instruction* pc,
                                                 const Operand& p1, const Operand& p2, int event,
                                                 const RuntimeError& error) {
    const bool bitwise = (event >= tm_band && event <= tm_shr) || event == tm_bnot;
    if (bitwise) {
        if (p1.value.is_number() && p2.value.is_number()) {
            const Operand& culprit = to_integer(p1.value) ? p2 : p1;
            return raise(state, pc,
                         std::format("number{} has no integer representation",
                                     operand_info(state, pc, culprit)));
        }
    } else if (p1.value.is_string() || p2.value.is_string()) {
        return raise(state, pc, error.message);
    }
    const Operand& culprit = p1.value.is_number() ? p2 : p1;
    return type_error(state, pc, culprit.value,
                      bitwise ? "perform bitwise operation on" : "perform arithmetic on",
                      operand_info(state, pc, culprit));
}

// MMBIN, MMBINI and MMBINK: the operation again, the long way (string
// operands, errors). The result goes where the instruction before wanted it.
[[gnu::noinline]] Status binary_slow(State* state, const Instruction* pc, Tag* tags,
                                     std::uint64_t* vals, const Operand& p1, const Operand& p2,
                                     int event) {
    const int result = get_a(pc[-2]);
    const ArithResult r = arith_event(event, p1.value, p2.value);
    if (r) [[likely]] {
        set_register(tags, vals, result, *r);
        return Status::Ok;
    }
    return binary_error(state, pc, p1, p2, event, r.error());
}

// UNM and BNOT on anything but plain numbers.
[[gnu::noinline]] Status unary_slow(State* state, const Instruction* pc, Tag* tags,
                                    std::uint64_t* vals, int a, int b, int event) {
    const Operand operand{.value = register_value(tags, vals, b), .reg = b};
    const ArithResult r = arith_event(event, operand.value, operand.value);
    if (r) {
        set_register(tags, vals, a, *r);
        return Status::Ok;
    }
    return binary_error(state, pc, operand, operand, event, r.error());
}

// LT and LE when not both are integers: numbers of mixed kinds compared
// exactly, strings, or the error.
[[gnu::noinline]] bool order_slow(State* state, const Instruction* pc, Value a, Value b,
                                  bool less_or_equal, bool& cond) {
    const CompareResult r = less_or_equal ? less_equal(a, b) : less_than(a, b);
    if (!r) {
        raise(state, pc, r.error().message);
        return false;
    }
    cond = *r;
    return true;
}

// LTI and friends with a value that is not a number (luaT_callorderiTM):
// without metamethods, always the order error, with the operands in the
// order the source wrote them.
[[gnu::cold, gnu::noinline]] bool order_immediate_slow(State* state, const Instruction* pc, Value a,
                                                       int immediate, bool is_float, bool flip,
                                                       bool less_or_equal, bool& cond) {
    const Value number =
        is_float ? Value::number(static_cast<double>(immediate)) : Value::integer(immediate);
    return flip ? order_slow(state, pc, number, a, less_or_equal, cond)
                : order_slow(state, pc, a, number, less_or_equal, cond);
}

// ---- Concatenation ----

[[gnu::noinline]] Status concat_registers(State* state, const Instruction* pc, Tag* tags,
                                          std::uint64_t* vals, int a, int n) {
    // luaV_concat: R[A] .. ... .. R[A+n-1], the whole chain at once.
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
    for (int j = 0; j < n; ++j) {
        values[static_cast<std::size_t>(j)] = register_value(tags, vals, a + j);
    }
    const auto r = concatenate(state->heap(), values);
    if (r) [[likely]] {
        set_register(tags, vals, a, *r);
        return Status::Ok;
    }
    // luaG_concaterror: the rightmost operand that is neither a string nor
    // a number, except that the last pair names its left one when both are.
    const auto joinable = [](Value v) { return v.is_string() || v.is_number(); };
    int culprit = n - 1;
    while (culprit > 0 && joinable(values[static_cast<std::size_t>(culprit)])) {
        --culprit;
    }
    if (culprit == n - 1 && culprit > 0 &&
        !joinable(values[static_cast<std::size_t>(culprit - 1)])) {
        --culprit;
    }
    return type_error(state, pc, values[static_cast<std::size_t>(culprit)], "concatenate",
                      register_info_here(state, pc, a + culprit));
}

// ---- Tables (only the globals until stage 6) ----

constexpr std::string_view not_string_key = "table keys other than strings are not supported yet";

// K[index], a string constant (the compiler only puts strings there).
const String* string_constant(const Value* k, int index) {
    return std::bit_cast<const String*>(k[index].payload());
}

// RK(C): a constant when the k bit is set, a register otherwise.
Value rk_value(const Tag* tags, const std::uint64_t* vals, const Value* k, Instruction i) {
    return get_k(i) ? k[get_c(i)] : register_value(tags, vals, get_c(i));
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
          std::format("bad 'for' {} (number expected, got {})", what, type_name(v.type())));
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
// (growing uses memory). False on "stack overflow" (E6.7), with the message
// in error_message.
[[gnu::noinline]] bool grow_for_call(State* state, std::uint32_t n) {
    if (state->heap().wants_step()) {
        collect_step(state);
    }
    return state->grow_stack(n);
}

// precallC with luaD_poscall: runs the C++ function in slot `func` with the
// arguments up to `top`, and leaves `wanted` of its results at `func`. A C++
// function gets min_stack free slots, and its own frame. False when the stack
// cannot grow ("stack overflow" in error_message).
bool call_c(State* state, std::uint32_t func, std::uint32_t top, int wanted);

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

bool call_c(State* state, std::uint32_t func, std::uint32_t top, int wanted) {
    state->top = top;
    if (state->stack_last - top <= min_stack && !grow_for_call(state, min_stack)) {
        return false;
    }
    CallInfo* ci = state->next_call_info();
    ci->func = func;
    ci->top = top + min_stack;
    ci->wanted = wanted;
    ci->fresh = false;
    ci->saved_pc = nullptr;
    ci->constants = nullptr;
    const auto function = std::bit_cast<CFunction>(state->stack_values[func]);
    const int n = function(*state); // (errors from C++ functions: stage 8)
    move_results(state, func, n, wanted);
    state->ci = ci->previous;
    return true;
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

// The cold continuations of handlers.inc (VM_COLD_HANDLER).
#define LUA2_COLD_LIST(X)                                                                          \
    X(GrowAndRetry)                                                                                \
    X(CallOther)                                                                                   \
    X(SetUpvalBarrier)                                                                             \
    X(DivideByZero)                                                                                \
    X(ModuloByZero)                                                                                \
    X(FloatArith)                                                                                  \
    X(Unary)                                                                                       \
    X(LengthError)                                                                                 \
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
#define VM_COLD(name) goto cold_##name
#define VM_NEXT() goto dispatch
#define VM_RETRY()                                                                                 \
    --pc;                                                                                          \
    goto dispatch
#include "handlers.inc"
#undef VM_HANDLER
#undef VM_COLD_HANDLER
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

// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace

Status call(State& thread, std::uint32_t func, int wanted) {
    // ccall and luaD_precall, from C++: a Lua closure gets a fresh frame and
    // runs until it returns.
    State* state = &thread;
    constexpr int max_c_calls = 200; // LUAI_MAXCCALLS
    if (++state->c_calls >= max_c_calls) [[unlikely]] {
        --state->c_calls;
        state->error_message = "C stack overflow";
        return Status::Error;
    }
    Status status = Status::Ok;
    if (state->stack_tags[func] == Tag::CFunction) {
        status = call_c(state, func, state->top, wanted) ? Status::Ok : Status::Error;
    } else if (state->stack_tags[func] != Tag::LuaClosure) {
        state->error_message =
            std::format("attempt to call a {} value", type_name(state->get(func).type()));
        status = Status::Error;
    } else {
        const Proto* p = std::bit_cast<const LuaClosure*>(state->stack_values[func])->proto;
        const std::uint32_t size = p->max_stack_size;
        if (state->stack_last - state->top <= size && !grow_for_call(state, size)) {
            status = Status::Error; // "stack overflow"
        } else {
            CallInfo* ci = state->next_call_info();
            ci->func = func;
            ci->top = func + 1 + size;
            ci->wanted = wanted;
            ci->fresh = true;
            ci->saved_pc = p->code.data();
            ci->constants = p->constants.data();
            for (std::uint32_t n = state->top - func - 1; n < p->parameter_count; ++n) {
                state->stack_tags[state->top] = Tag::Nil;
                state->stack_values[state->top] = 0;
                ++state->top;
            }
            status = execute(state);
        }
    }
    --state->c_calls;
    return status;
}

} // namespace lua2
