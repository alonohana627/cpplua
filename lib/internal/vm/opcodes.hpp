#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace lua2 {

// The VM's instructions (docs/plan/vm.md, part 0): Lua 5.4's format and
// opcodes, from its lopcodes.h. One instruction is one 32-bit word:
//
//            31       24 23      16 15 14       7 6      0
//   ABC      |    C    |    B    | k |    A    |   op   |
//   ABx      |          Bx (17)        |    A    |   op   |
//   AsBx     |         sBx (17)        |    A    |   op   |
//   Ax       |               Ax (25)             |   op   |
//   sJ       |               sJ (25)             |   op   |
//
// Signed fields are stored with an offset ("excess K"): the stored value is
// the field plus half its range, so it fits in an unsigned field.

using Instruction = std::uint32_t;

// Every opcode, in Lua 5.4's order. The comment is what it does; R[x] is a
// register, K[x] a constant, U[x] an upvalue.
enum class OpCode : std::uint8_t {
    Move,       // A B      R[A] := R[B]
    LoadI,      // A sBx    R[A] := sBx
    LoadF,      // A sBx    R[A] := (float)sBx
    LoadK,      // A Bx     R[A] := K[Bx]
    LoadKX,     // A        R[A] := K[extra arg]
    LoadFalse,  // A        R[A] := false
    LFalseSkip, // A        R[A] := false; pc++
    LoadTrue,   // A        R[A] := true
    LoadNil,    // A B      R[A], R[A+1], ..., R[A+B] := nil
    GetUpval,   // A B      R[A] := U[B]
    SetUpval,   // A B      U[B] := R[A]
    GetTabUp,   // A B C    R[A] := U[B][K[C]:string]
    GetTable,   // A B C    R[A] := R[B][R[C]]
    GetI,       // A B C    R[A] := R[B][C]
    GetField,   // A B C    R[A] := R[B][K[C]:string]
    SetTabUp,   // A B C    U[A][K[B]:string] := RK(C)
    SetTable,   // A B C    R[A][R[B]] := RK(C)
    SetI,       // A B C    R[A][B] := RK(C)
    SetField,   // A B C    R[A][K[B]:string] := RK(C)
    NewTable,   // A B C k  R[A] := {}
    Self,       // A B C    R[A+1] := R[B]; R[A] := R[B][RK(C):string]
    AddI,       // A B sC   R[A] := R[B] + sC
    AddK,       // A B C    R[A] := R[B] + K[C]:number
    SubK,       // A B C    R[A] := R[B] - K[C]:number
    MulK,       // A B C    R[A] := R[B] * K[C]:number
    ModK,       // A B C    R[A] := R[B] % K[C]:number
    PowK,       // A B C    R[A] := R[B] ^ K[C]:number
    DivK,       // A B C    R[A] := R[B] / K[C]:number
    IDivK,      // A B C    R[A] := R[B] // K[C]:number
    BAndK,      // A B C    R[A] := R[B] & K[C]:integer
    BOrK,       // A B C    R[A] := R[B] | K[C]:integer
    BXorK,      // A B C    R[A] := R[B] ~ K[C]:integer
    ShrI,       // A B sC   R[A] := R[B] >> sC
    ShlI,       // A B sC   R[A] := sC << R[B]
    Add,        // A B C    R[A] := R[B] + R[C]
    Sub,        // A B C    R[A] := R[B] - R[C]
    Mul,        // A B C    R[A] := R[B] * R[C]
    Mod,        // A B C    R[A] := R[B] % R[C]
    Pow,        // A B C    R[A] := R[B] ^ R[C]
    Div,        // A B C    R[A] := R[B] / R[C]
    IDiv,       // A B C    R[A] := R[B] // R[C]
    BAnd,       // A B C    R[A] := R[B] & R[C]
    BOr,        // A B C    R[A] := R[B] | R[C]
    BXor,       // A B C    R[A] := R[B] ~ R[C]
    Shl,        // A B C    R[A] := R[B] << R[C]
    Shr,        // A B C    R[A] := R[B] >> R[C]
    MmBin,      // A B C    the slow path of the previous op, over R[A] and R[B]
    MmBinI,     // A sB C k the slow path of the previous op, over R[A] and sB
    MmBinK,     // A B C k  the slow path of the previous op, over R[A] and K[B]
    Unm,        // A B      R[A] := -R[B]
    BNot,       // A B      R[A] := ~R[B]
    Not,        // A B      R[A] := not R[B]
    Len,        // A B      R[A] := #R[B]
    Concat,     // A B      R[A] := R[A] .. ... .. R[A + B - 1]
    Close,      // A        close all upvalues >= R[A]
    Tbc,        // A        mark variable A "to be closed"
    Jmp,        // sJ       pc += sJ
    Eq,         // A B k    if ((R[A] == R[B]) ~= k) then pc++
    Lt,         // A B k    if ((R[A] <  R[B]) ~= k) then pc++
    Le,         // A B k    if ((R[A] <= R[B]) ~= k) then pc++
    EqK,        // A B k    if ((R[A] == K[B]) ~= k) then pc++
    EqI,        // A sB k   if ((R[A] == sB) ~= k) then pc++
    LtI,        // A sB k   if ((R[A] < sB) ~= k) then pc++
    LeI,        // A sB k   if ((R[A] <= sB) ~= k) then pc++
    GtI,        // A sB k   if ((R[A] > sB) ~= k) then pc++
    GeI,        // A sB k   if ((R[A] >= sB) ~= k) then pc++
    Test,       // A k      if (not R[A] == k) then pc++
    TestSet,    // A B k    if (not R[B] == k) then pc++ else R[A] := R[B]
    Call,       // A B C    R[A], ..., R[A+C-2] := R[A](R[A+1], ..., R[A+B-1])
    TailCall,   // A B C k  return R[A](R[A+1], ..., R[A+B-1])
    Return,     // A B C k  return R[A], ..., R[A+B-2]
    Return0,    //          return
    Return1,    // A        return R[A]
    ForLoop,    // A Bx     update counters; if the loop continues then pc -= Bx
    ForPrep,    // A Bx     check values and prepare counters; if not to run then pc += Bx + 1
    TForPrep,   // A Bx     create upvalue for R[A + 3]; pc += Bx
    TForCall,   // A C      R[A+4], ..., R[A+3+C] := R[A](R[A+1], R[A+2])
    TForLoop,   // A Bx     if R[A+2] ~= nil then { R[A] = R[A+2]; pc -= Bx }
    SetList,    // A B C k  R[A][C+i] := R[A+i], 1 <= i <= B
    Closure,    // A Bx     R[A] := closure(KPROTO[Bx])
    VarArg,     // A C      R[A], R[A+1], ..., R[A+C-2] = vararg
    VarArgPrep, // A        adjust the vararg parameters
    ExtraArg,   // Ax       an extra (larger) argument for the previous opcode
};

inline constexpr int opcode_count = static_cast<int>(OpCode::ExtraArg) + 1;
static_assert(opcode_count == 83); // Lua 5.4's NUM_OPCODES

// ---- Field sizes and positions (lopcodes.h) ----

inline constexpr int size_op = 7;
inline constexpr int size_a = 8;
inline constexpr int size_b = 8;
inline constexpr int size_c = 8;
inline constexpr int size_bx = size_c + size_b + 1; // 17
inline constexpr int size_ax = size_bx + size_a;    // 25
inline constexpr int size_sj = size_bx + size_a;    // 25

inline constexpr int pos_op = 0;
inline constexpr int pos_a = pos_op + size_op; // 7
inline constexpr int pos_k = pos_a + size_a;   // 15
inline constexpr int pos_b = pos_k + 1;        // 16
inline constexpr int pos_c = pos_b + size_b;   // 24
inline constexpr int pos_bx = pos_k;           // 15
inline constexpr int pos_ax = pos_a;           // 7
inline constexpr int pos_sj = pos_a;           // 7

inline constexpr int max_a = 255;       // 2^8 - 1
inline constexpr int max_b = 255;       // 2^8 - 1
inline constexpr int max_c = 255;       // 2^8 - 1
inline constexpr int max_bx = 131071;   // 2^17 - 1
inline constexpr int max_ax = 33554431; // 2^25 - 1
inline constexpr int max_sj = 33554431; // 2^25 - 1

// The offsets of the signed fields: half the field's range.
inline constexpr int offset_sbx = max_bx / 2; // 65535
inline constexpr int offset_sj = max_sj / 2;  // 16777215
inline constexpr int offset_sc = max_c / 2;   // 127

namespace detail {

constexpr Instruction mask(int size) {
    return (Instruction{1} << static_cast<unsigned>(size)) - 1;
}

constexpr int field(Instruction i, int pos, int size) {
    return static_cast<int>((i >> static_cast<unsigned>(pos)) & mask(size));
}

constexpr Instruction with_field(Instruction i, int pos, int size, int value) {
    const Instruction m = mask(size) << static_cast<unsigned>(pos);
    return (i & ~m) | ((static_cast<Instruction>(value) << static_cast<unsigned>(pos)) & m);
}

} // namespace detail

// ---- Reading fields ----

constexpr OpCode get_op(Instruction i) {
    return static_cast<OpCode>(detail::field(i, pos_op, size_op));
}

constexpr int get_a(Instruction i) {
    return detail::field(i, pos_a, size_a);
}

constexpr int get_b(Instruction i) {
    return detail::field(i, pos_b, size_b);
}

constexpr int get_sb(Instruction i) {
    return get_b(i) - offset_sc;
}

constexpr int get_c(Instruction i) {
    return detail::field(i, pos_c, size_c);
}

constexpr int get_sc(Instruction i) {
    return get_c(i) - offset_sc;
}

constexpr bool get_k(Instruction i) {
    return detail::field(i, pos_k, 1) != 0;
}

constexpr int get_bx(Instruction i) {
    return detail::field(i, pos_bx, size_bx);
}

constexpr int get_sbx(Instruction i) {
    return get_bx(i) - offset_sbx;
}

constexpr int get_ax(Instruction i) {
    return detail::field(i, pos_ax, size_ax);
}

constexpr int get_sj(Instruction i) {
    return detail::field(i, pos_sj, size_sj) - offset_sj;
}

// ---- Changing fields (the compiler patches jumps and registers) ----

constexpr Instruction set_a(Instruction i, int a) {
    return detail::with_field(i, pos_a, size_a, a);
}

constexpr Instruction set_b(Instruction i, int b) {
    return detail::with_field(i, pos_b, size_b, b);
}

constexpr Instruction set_c(Instruction i, int c) {
    return detail::with_field(i, pos_c, size_c, c);
}

constexpr Instruction set_k(Instruction i, bool k) {
    return detail::with_field(i, pos_k, 1, k ? 1 : 0);
}

constexpr Instruction set_bx(Instruction i, int bx) {
    return detail::with_field(i, pos_bx, size_bx, bx);
}

constexpr Instruction set_sj(Instruction i, int sj) {
    return detail::with_field(i, pos_sj, size_sj, sj + offset_sj);
}

// ---- Making instructions ----
// The arguments must fit their fields: A, B and C in 0..255, sB and sC in
// -127..128, Bx in 0..2^17-1, sBx in -(2^16-1)..2^16, Ax in 0..2^25-1, sJ in
// -(2^24-1)..2^24.

constexpr Instruction make_abc(OpCode op, int a, int b, int c, bool k = false) {
    auto i = static_cast<Instruction>(op);
    i = set_a(i, a);
    i = set_b(i, b);
    i = set_c(i, c);
    return set_k(i, k);
}

constexpr Instruction make_abx(OpCode op, int a, int bx) {
    return set_bx(set_a(static_cast<Instruction>(op), a), bx);
}

constexpr Instruction make_asbx(OpCode op, int a, int sbx) {
    return make_abx(op, a, sbx + offset_sbx);
}

constexpr Instruction make_ax(OpCode op, int ax) {
    return detail::with_field(static_cast<Instruction>(op), pos_ax, size_ax, ax);
}

constexpr Instruction make_sj(OpCode op, int sj) {
    return set_sj(static_cast<Instruction>(op), sj);
}

// ---- What each opcode is like (lopcodes.c's luaP_opmodes) ----

enum class OpFormat : std::uint8_t { ABC, ABx, AsBx, Ax, SJ };

struct OpInfo {
    std::string_view name; // as luac -l prints it
    OpFormat format;
    bool sets_a;     // writes R[A]
    bool is_test;    // the next instruction is a jump, taken or skipped
    bool uses_top;   // reads the stack top set by the previous instruction
    bool sets_top;   // leaves the stack top for the next instruction
    bool metamethod; // an MMBIN instruction
};

namespace detail {

constexpr OpInfo info(std::string_view name, OpFormat format, bool sets_a, bool is_test = false,
                      bool uses_top = false, bool sets_top = false, bool metamethod = false) {
    return OpInfo{
        .name = name,
        .format = format,
        .sets_a = sets_a,
        .is_test = is_test,
        .uses_top = uses_top,
        .sets_top = sets_top,
        .metamethod = metamethod,
    };
}

} // namespace detail

inline constexpr std::array<OpInfo, opcode_count> op_infos = [] {
    using enum OpFormat;
    using detail::info;
    return std::array<OpInfo, opcode_count>{
        info("MOVE", ABC, true),
        info("LOADI", AsBx, true),
        info("LOADF", AsBx, true),
        info("LOADK", ABx, true),
        info("LOADKX", ABx, true),
        info("LOADFALSE", ABC, true),
        info("LFALSESKIP", ABC, true),
        info("LOADTRUE", ABC, true),
        info("LOADNIL", ABC, true),
        info("GETUPVAL", ABC, true),
        info("SETUPVAL", ABC, false),
        info("GETTABUP", ABC, true),
        info("GETTABLE", ABC, true),
        info("GETI", ABC, true),
        info("GETFIELD", ABC, true),
        info("SETTABUP", ABC, false),
        info("SETTABLE", ABC, false),
        info("SETI", ABC, false),
        info("SETFIELD", ABC, false),
        info("NEWTABLE", ABC, true),
        info("SELF", ABC, true),
        info("ADDI", ABC, true),
        info("ADDK", ABC, true),
        info("SUBK", ABC, true),
        info("MULK", ABC, true),
        info("MODK", ABC, true),
        info("POWK", ABC, true),
        info("DIVK", ABC, true),
        info("IDIVK", ABC, true),
        info("BANDK", ABC, true),
        info("BORK", ABC, true),
        info("BXORK", ABC, true),
        info("SHRI", ABC, true),
        info("SHLI", ABC, true),
        info("ADD", ABC, true),
        info("SUB", ABC, true),
        info("MUL", ABC, true),
        info("MOD", ABC, true),
        info("POW", ABC, true),
        info("DIV", ABC, true),
        info("IDIV", ABC, true),
        info("BAND", ABC, true),
        info("BOR", ABC, true),
        info("BXOR", ABC, true),
        info("SHL", ABC, true),
        info("SHR", ABC, true),
        info("MMBIN", ABC, false, false, false, false, true),
        info("MMBINI", ABC, false, false, false, false, true),
        info("MMBINK", ABC, false, false, false, false, true),
        info("UNM", ABC, true),
        info("BNOT", ABC, true),
        info("NOT", ABC, true),
        info("LEN", ABC, true),
        info("CONCAT", ABC, true),
        info("CLOSE", ABC, false),
        info("TBC", ABC, false),
        info("JMP", SJ, false),
        info("EQ", ABC, false, true),
        info("LT", ABC, false, true),
        info("LE", ABC, false, true),
        info("EQK", ABC, false, true),
        info("EQI", ABC, false, true),
        info("LTI", ABC, false, true),
        info("LEI", ABC, false, true),
        info("GTI", ABC, false, true),
        info("GEI", ABC, false, true),
        info("TEST", ABC, false, true),
        info("TESTSET", ABC, true, true),
        info("CALL", ABC, true, false, true, true),
        info("TAILCALL", ABC, true, false, true, true),
        info("RETURN", ABC, false, false, true),
        info("RETURN0", ABC, false),
        info("RETURN1", ABC, false),
        info("FORLOOP", ABx, true),
        info("FORPREP", ABx, true),
        info("TFORPREP", ABx, false),
        info("TFORCALL", ABC, false),
        info("TFORLOOP", ABx, true),
        info("SETLIST", ABC, false, false, true),
        info("CLOSURE", ABx, true),
        info("VARARG", ABC, true, false, false, true),
        info("VARARGPREP", ABC, true, false, true),
        info("EXTRAARG", Ax, false),
    };
}();

constexpr const OpInfo& op_info(OpCode op) {
    return op_infos.at(static_cast<std::size_t>(op));
}

} // namespace lua2
