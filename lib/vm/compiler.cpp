// The compiler: Lua 5.4's lcode.c, and the code-generating half of
// lparser.c, ported to walk lua2's syntax tree. Function names in the
// comments are Lua's, so the two can be read side by side. The parser has
// already checked everything Lua checks while parsing (names, gotos, const
// assignments, limits on locals and nesting), so only the code-size limits
// can fail here.

#include "vm/compiler.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "overloaded.hpp"
#include "value/arith.hpp"
#include "value/bitwise.hpp"
#include "value/compare.hpp"
#include "value/number.hpp"
#include "vm/opcodes.hpp"

namespace lua2 {

namespace {

constexpr int no_jump = -1;          // NO_JUMP: the end of a jump list
constexpr int no_reg = max_a;        // NO_REG
constexpr int max_registers = 255;   // MAXREGS
constexpr int max_index_rk = max_b;  // MAXINDEXRK
constexpr int fields_per_flush = 50; // LFIELDS_PER_FLUSH
constexpr int max_upvalues = 255;    // MAXUPVAL
constexpr int multiple_results = -1; // LUA_MULTRET

// What an expression is while being compiled (Lua's expkind). Code is
// generated as late as possible, so the descriptor says where the value is
// or how to get it.
enum class ExpKind : std::uint8_t {
    Void,     // no value (an empty list)
    Nil,      // constant nil
    True,     // constant true
    False,    // constant false
    K,        // a constant in the constant list; info = its index
    KFlt,     // a float constant; nval
    KInt,     // an integer constant; ival
    KStr,     // a string constant; strval
    NonReloc, // in a fixed register; info = the register
    Local,    // a local variable; var_ridx = its register, var_vidx = its compiler index
    Upval,    // an upvalue; info = its index
    Const,    // a compile-time <const>; info = its index in the variable list
    Indexed,  // t[k], both in registers; ind_t = t's register, ind_idx = k's
    IndexUp,  // upvalue[K]; ind_t = the upvalue, ind_idx = the constant (a short string)
    IndexI,   // t[i] with a small integer i; ind_t = t's register, ind_idx = i
    IndexStr, // t[K] with a short string constant; ind_t = t's register, ind_idx = K
    Jmp,      // a test or comparison; info = its jump instruction
    Reloc,    // can be put in any register; info = the instruction that computes it
    Call,     // a call; info = the CALL instruction
    Vararg,   // '...'; info = the VARARG instruction
};

// An int position (pc, register, constant) as a vector index.
constexpr std::size_t index(int i) {
    return static_cast<std::size_t>(i);
}

constexpr bool is_indexed(ExpKind k) {
    return ExpKind::Indexed <= k && k <= ExpKind::IndexStr;
}

constexpr bool has_multiple_results(ExpKind k) {
    return k == ExpKind::Call || k == ExpKind::Vararg;
}

// An expression descriptor (Lua's expdesc), plus the lists of jumps that
// make its value true or false (from `and`, `or`, comparisons). Lua keeps
// the fields in a union; here each has its own field.
struct ExpDesc {
    ExpKind k = ExpKind::Void;
    int info = 0;
    std::int64_t ival = 0;
    double nval = 0;
    const String* strval = nullptr;
    int ind_t = 0;
    int ind_idx = 0;
    int var_ridx = 0;
    int var_vidx = 0;
    int t = no_jump; // exits when true
    int f = no_jump; // exits when false
};

// Lua's BinOpr and UnOpr, in Lua's order, which matches the opcodes and
// the metamethod events.
enum class BinOpr : std::uint8_t {
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
    Concat,
    Eq,
    Lt,
    Le,
    Ne,
    Gt,
    Ge,
    And,
    Or,
};

enum class UnOpr : std::uint8_t { Minus, BNot, Not, Len };

constexpr BinOpr binary_operator(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return BinOpr::Add;
    case BinaryOp::Subtract: return BinOpr::Sub;
    case BinaryOp::Multiply: return BinOpr::Mul;
    case BinaryOp::Divide: return BinOpr::Div;
    case BinaryOp::FloorDivide: return BinOpr::IDiv;
    case BinaryOp::Modulo: return BinOpr::Mod;
    case BinaryOp::Power: return BinOpr::Pow;
    case BinaryOp::Concat: return BinOpr::Concat;
    case BinaryOp::Equal: return BinOpr::Eq;
    case BinaryOp::NotEqual: return BinOpr::Ne;
    case BinaryOp::Less: return BinOpr::Lt;
    case BinaryOp::LessEqual: return BinOpr::Le;
    case BinaryOp::Greater: return BinOpr::Gt;
    case BinaryOp::GreaterEqual: return BinOpr::Ge;
    case BinaryOp::And: return BinOpr::And;
    case BinaryOp::Or: return BinOpr::Or;
    case BinaryOp::BitwiseAnd: return BinOpr::BAnd;
    case BinaryOp::BitwiseOr: return BinOpr::BOr;
    case BinaryOp::BitwiseXor: return BinOpr::BXor;
    case BinaryOp::ShiftLeft: return BinOpr::Shl;
    case BinaryOp::ShiftRight: return BinOpr::Shr;
    }
    return BinOpr::Add; // not reached
}

constexpr UnOpr unary_operator(UnaryOp op) {
    switch (op) {
    case UnaryOp::Negate: return UnOpr::Minus;
    case UnaryOp::BitwiseNot: return UnOpr::BNot;
    case UnaryOp::Not: return UnOpr::Not;
    case UnaryOp::Length: return UnOpr::Len;
    }
    return UnOpr::Minus; // not reached
}

// binopr2op: the opcode of a binary operator, counted from a base.
constexpr OpCode opcode_from(BinOpr op, BinOpr base_operator, OpCode base) {
    return static_cast<OpCode>(static_cast<int>(op) - static_cast<int>(base_operator) +
                               static_cast<int>(base));
}

// binopr2TM: the metamethod event of an arithmetic or bitwise operator
// (TM_ADD is 6, in ltm.h's order).
constexpr int event_of(BinOpr op) {
    constexpr int tm_add = 6;
    return static_cast<int>(op) - static_cast<int>(BinOpr::Add) + tm_add;
}

constexpr int tm_shl = 16; // TM_SHL
constexpr int tm_shr = 17; // TM_SHR

// fitsC: whether i fits an sC operand (-127..128).
constexpr bool fits_c(std::int64_t i) {
    return static_cast<std::uint64_t>(i) + offset_sc <= static_cast<std::uint64_t>(max_c);
}

// fitsBx: whether i fits an sBx operand.
constexpr bool fits_bx(std::int64_t i) {
    return -offset_sbx <= i && i <= max_bx - offset_sbx;
}

// int2sC
constexpr int to_sc(int i) {
    return i + offset_sc;
}

// The kinds of local variables (Lua's VDKREG, RDKCONST, RDKTOCLOSE, RDKCTC).
enum class VarKind : std::uint8_t {
    Regular,
    Const,            // <const>, in a register
    ToClose,          // <close>
    CompileTimeConst, // <const> with a constant value: no register at all
};

// An active local variable (Lua's Vardesc).
struct VarDesc {
    Value k; // the value of a compile-time constant
    VarKind kind = VarKind::Regular;
    int ridx = 0; // its register
    int pidx = 0; // its index in the proto's list of locals (debug information)
    const String* name = nullptr;
};

// A label, or a goto waiting for its label (Lua's Labeldesc).
struct LabelDesc {
    const String* name = nullptr;
    int pc = 0;   // the label's position, or the goto's jump
    int line = 0; // where it appeared
    int nactvar = 0;
    bool close = false; // a goto that leaves the scope of upvalues
};

// A block being compiled (Lua's BlockCnt).
struct BlockCnt {
    BlockCnt* previous = nullptr;
    int firstlabel = 0; // its first label in the list of labels
    int firstgoto = 0;  // its first pending goto
    int nactvar = 0;    // active locals outside the block
    bool upval = false; // some variable of the block is an upvalue
    bool isloop = false;
    bool insidetbc = false; // inside the scope of a to-be-closed variable
};

// A function being compiled (Lua's FuncState).
struct FuncState {
    Proto* f = nullptr;
    FuncState* prev = nullptr;
    BlockCnt* bl = nullptr;
    int lasttarget = 0; // the last position marked as a jump target
    int firstlocal = 0; // its first variable in the list of active variables
    int firstlabel = 0;
    int nactvar = 0;
    int freereg = 0;
    bool needclose = false; // has upvalues to close when returning

    [[nodiscard]] int pc() const { return static_cast<int>(f->code.size()); }
};

// The key a constant is cached under (the scanner table of lparser.c),
// with Lua's table key rules: a float with an integer value is that
// integer, so 1.0 and 1 share a key, which is why numberK makes another key
// for such floats.
struct ConstKey {
    enum class Kind : std::uint8_t { Nil, False, True, Integer, Float, String };
    Kind kind = Kind::Nil;
    std::uint64_t bits = 0;

    bool operator==(const ConstKey&) const = default;
};

struct ConstKeyHash {
    std::size_t operator()(const ConstKey& key) const {
        return std::hash<std::uint64_t>{}(key.bits) ^ static_cast<std::size_t>(key.kind);
    }
};

ConstKey key_of(Value v) {
    switch (v.tag()) {
    case Tag::Nil: return {.kind = ConstKey::Kind::Nil};
    case Tag::False: return {.kind = ConstKey::Kind::False};
    case Tag::True: return {.kind = ConstKey::Kind::True};
    case Tag::Integer: return {.kind = ConstKey::Kind::Integer, .bits = v.payload()};
    case Tag::Float:
        if (const auto i = float_to_int(v.as_float())) {
            return {.kind = ConstKey::Kind::Integer, .bits = std::bit_cast<std::uint64_t>(*i)};
        }
        return {.kind = ConstKey::Kind::Float, .bits = v.payload()};
    case Tag::ShortString:
    case Tag::LongString:
    case Tag::CFunction:
    case Tag::LuaClosure:
    case Tag::CClosure:
    case Tag::LightUserdata:
    case Tag::Userdata:
    case Tag::Thread:
    case Tag::Table: break;
    }
    return {.kind = ConstKey::Kind::String, .bits = v.payload()};
}

// luaV_rawequalobj for two constants with the same tag.
bool raw_equal(Value a, Value b) {
    if (a.is_string()) {
        return a.as_string() == b.as_string() || a.as_string()->view() == b.as_string()->view();
    }
    return equals(a, b);
}

class Compiler {
public:
    Compiler(Heap& heap, const Ast& ast, std::string_view source, std::string_view chunk_name)
        : heap_(heap), ast_(ast), chunk_name_(chunk_name), source_(heap.new_string(source)),
          env_name_(heap.new_string("_ENV")), break_name_(heap.new_string("break")),
          for_state_name_(heap.new_string("(for state)")) {}

    std::expected<Proto*, SyntaxError> compile_main();

private:
    // ---- Errors ----

    void error(std::string_view message) {
        if (!error_) {
            error_ = make_syntax_error(chunk_name_, last_line_, message, "");
        }
    }

    // ---- Strings ----

    const String* string_of(TextRange text);
    static void codestring(ExpDesc& e, const String* s);

    // ---- lcode.c: instructions ----

    int code(Instruction i);
    int code_abck(OpCode op, int a, int b, int c, bool k);

    int code_abc(OpCode op, int a, int b, int c) { return code_abck(op, a, b, c, false); }

    int code_abx(OpCode op, int a, int bx);
    int code_asbx(OpCode op, int a, int sbx);
    int code_sj(OpCode op, int sj);
    int code_extra_arg(int a);
    int code_k(int reg, int k);
    void fixline(int line);
    void remove_last_instruction();
    Instruction* previous_instruction();

    Instruction& instruction(const ExpDesc& e) { return fs_->f->code[index(e.info)]; }

    // ---- lcode.c: jumps ----

    int getjump(int pc);
    void fixjump(int pc, int dest);
    void concat(int& l1, int l2);
    int jump();
    void ret(int first, int nret);
    int condjump(OpCode op, int a, int b, int c, bool k);
    int getlabel();
    Instruction* getjumpcontrol(int pc);
    bool patchtestreg(int node, int reg);
    void removevalues(int list);
    void patchlistaux(int list, int vtarget, int reg, int dtarget);
    void patchlist(int list, int target);
    void patchtohere(int list);

    // ---- lcode.c: registers ----

    void checkstack(int n);
    void reserveregs(int n);
    void freereg(int reg);
    void freeregs(int r1, int r2);
    void freeexp(const ExpDesc& e);
    void freeexps(const ExpDesc& e1, const ExpDesc& e2);

    // ---- lcode.c: constants ----

    int addk(ConstKey key, Value v);
    int string_k(const String* s);
    int int_k(std::int64_t n);
    int number_k(double r);
    int bool_f();
    int bool_t();
    int nil_k();
    void load_int(int reg, std::int64_t i);
    void load_float(int reg, double f);
    static bool tonumeral(const ExpDesc& e, Value* v);
    bool exp2const(const ExpDesc& e, Value& v);
    static void const2exp(Value v, ExpDesc& e);

    // ---- lcode.c: expressions ----

    void nil(int from, int n);
    void setreturns(ExpDesc& e, int nresults);

    void setmultret(ExpDesc& e) { setreturns(e, multiple_results); }

    void str2k(ExpDesc& e);
    void setoneret(ExpDesc& e);
    void dischargevars(ExpDesc& e);
    void discharge2reg(ExpDesc& e, int reg);
    void discharge2anyreg(ExpDesc& e);
    int code_loadbool(int a, OpCode op);
    bool need_value(int list);
    void exp2reg(ExpDesc& e, int reg);
    void exp2nextreg(ExpDesc& e);
    int exp2anyreg(ExpDesc& e);
    void exp2anyregup(ExpDesc& e);
    void exp2val(ExpDesc& e);
    bool exp2k(ExpDesc& e);
    bool exp2rk(ExpDesc& e);
    void code_abrk(OpCode op, int a, int b, ExpDesc& ec);
    void storevar(const ExpDesc& var, ExpDesc& ex);
    void self(ExpDesc& e, ExpDesc& key);
    void negatecondition(const ExpDesc& e);
    int jumponcond(ExpDesc& e, bool cond);
    void goiftrue(ExpDesc& e);
    void goiffalse(ExpDesc& e);
    void codenot(ExpDesc& e);
    [[nodiscard]] bool is_kstr(const ExpDesc& e) const;
    void indexed(ExpDesc& t, ExpDesc& k);
    static bool constfolding(BinOpr op, ExpDesc& e1, const ExpDesc& e2);
    static bool constfolding_unary(UnOpr op, ExpDesc& e);
    void codeunexpval(OpCode op, ExpDesc& e, int line);
    void finishbinexpval(ExpDesc& e1, ExpDesc& e2, OpCode op, int v2, bool flip, int line,
                         OpCode mmop, int event);
    void codebinexpval(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line);
    void codebini(OpCode op, ExpDesc& e1, ExpDesc& e2, bool flip, int line, int event);
    void codebink(BinOpr opr, ExpDesc& e1, ExpDesc& e2, bool flip, int line);
    bool finishbinexpneg(ExpDesc& e1, ExpDesc& e2, OpCode op, int line, int event);
    void codebinnok(BinOpr opr, ExpDesc& e1, ExpDesc& e2, bool flip, int line);
    void codearith(BinOpr opr, ExpDesc& e1, ExpDesc& e2, bool flip, int line);
    void codecommutative(BinOpr op, ExpDesc& e1, ExpDesc& e2, int line);
    void codebitwise(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line);
    void codeorder(BinOpr opr, ExpDesc& e1, ExpDesc& e2);
    void codeeq(BinOpr opr, ExpDesc& e1, ExpDesc& e2);
    void prefix(UnOpr op, ExpDesc& e, int line);
    void infix(BinOpr op, ExpDesc& v);
    void codeconcat(ExpDesc& e1, ExpDesc& e2, int line);
    void posfix(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line);
    void settablesize(int pc, int ra, int asize, int hsize);
    void setlist(int base, int nelems, int tostore);
    void finish();

    // ---- lparser.c: variables ----

    int new_localvar(const String* name);
    VarDesc& getlocalvardesc(const FuncState* fs, int vidx);
    int reglevel(const FuncState* fs, int nvar);

    int nvarstack() { return reglevel(fs_, fs_->nactvar); }

    Proto::LocalInfo* localdebuginfo(const FuncState* fs, int vidx);
    void init_var(const FuncState* fs, ExpDesc& e, int vidx);
    void adjustlocalvars(int nvars);
    void removevars(int tolevel);
    static int searchupvalue(const FuncState* fs, const String* name);
    int newupvalue(FuncState* fs, const String* name, const ExpDesc& v);
    bool searchvar(const FuncState* fs, const String* n, ExpDesc& var);
    static void markupval(FuncState* fs, int level);
    static void marktobeclosed(FuncState* fs);
    void singlevaraux(FuncState* fs, const String* n, ExpDesc& var, bool base);
    void singlevar(const String* name, ExpDesc& var);
    void adjust_assign(int nvars, int nexps, ExpDesc& e);

    // ---- lparser.c: labels and blocks ----

    void solvegoto(std::size_t g, const LabelDesc& label);
    int findlabel(const String* name) const;
    int newlabelentry(std::vector<LabelDesc>& list, const String* name, int line, int pc);
    bool solvegotos(const LabelDesc& label);
    bool createlabel(const String* name, int line, bool last);
    void movegotosout(const BlockCnt& bl);
    void enterblock(BlockCnt& bl, bool isloop);
    void leaveblock();

    // ---- lparser.c: functions ----

    Proto* addprototype();
    void codeclosure(ExpDesc& v);
    void open_func(FuncState& fs, BlockCnt& bl);
    void close_func();
    void setvararg(int nparams);
    void body(ExpDesc& e, FunctionId id, int line);

    // ---- lparser.c: expressions ----

    [[nodiscard]] int first_line(ExprId id) const;
    int explist(IdRange<ExprId> list, ExpDesc& v);
    void funcargs(ExpDesc& f, IdRange<ExprId> arguments, int line, int end_line);
    void constructor(const TableExpr& table, ExpDesc& t, int end_line);
    void expr(ExprId id, ExpDesc& v);

    // ---- lparser.c: statements ----

    struct LhsAssign {
        LhsAssign* prev = nullptr;
        ExpDesc v;
    };

    void statlist(std::span<const StatId> statements, bool repeat_body = false);
    void statement(StatId id);
    void block(const Block& b);
    void check_conflict(LhsAssign* lh, const ExpDesc& v);
    void restassign(LhsAssign& lh, std::span<const ExprId> targets, IdRange<ExprId> values,
                    int nvars);
    int cond(ExprId condition);
    void labelstat(const String* name, int line, bool last);
    void gotostat(const GotoStat& s, StatId id);
    void whilestat(const WhileStat& s, StatId id);
    void repeatstat(const RepeatStat& s, StatId id);
    void exp1(ExprId e);
    void fixforjump(int pc, int dest, bool back);
    void forbody(int base, int line, int nvars, bool isgen, const Block& b);
    void fornum(const NumericForStat& s, int line);
    void forlist(const GenericForStat& s);
    void test_then_block(int& escapelist, const IfClause& clause, bool more);
    void ifstat(const IfStat& s, StatId id);
    void localfunc(const LocalFunctionStat& s, int line);
    void checktoclose(int level);
    void localstat(const LocalStat& s, StatId id);
    void funcstat(const FunctionStat& s, int line);
    void retstat(const ReturnStat& s);

    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members): owned by compile()'s caller
    Heap& heap_;
    const Ast& ast_;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::string_view chunk_name_;
    const String* source_;
    const String* env_name_;
    const String* break_name_;
    const String* for_state_name_;

    FuncState* fs_ = nullptr;
    int last_line_ = 1; // the line of the last token "read" (Lua's lastline)
    std::optional<SyntaxError> error_;

    // Lua's Dyndata: the variables of every open function (entries past
    // active_count_ are kept, as Lua reads them just after removing them),
    // pending gotos, and visible labels.
    std::vector<VarDesc> actvar_;
    int active_count_ = 0;
    std::vector<LabelDesc> gotos_;
    std::vector<LabelDesc> labels_;

    // The scanner table: where each constant went, shared by all functions.
    std::unordered_map<ConstKey, int, ConstKeyHash> constant_cache_;
    // One String per long string text, as Lua's lexer keeps them.
    std::unordered_map<std::string_view, const String*> long_strings_;
};

// ---- Strings ----

const String* Compiler::string_of(TextRange text) {
    const std::string_view bytes = ast_.text(text);
    if (bytes.size() <= max_short_length) {
        return heap_.new_string(bytes);
    }
    auto [it, added] = long_strings_.try_emplace(bytes, nullptr);
    if (added) {
        it->second = heap_.new_string(bytes);
    }
    return it->second;
}

void Compiler::codestring(ExpDesc& e, const String* s) {
    e.f = e.t = no_jump;
    e.k = ExpKind::KStr;
    e.strval = s;
}

void init_exp(ExpDesc& e, ExpKind k, int info) {
    e.f = e.t = no_jump;
    e.k = k;
    e.info = info;
}

bool has_jumps(const ExpDesc& e) {
    return e.t != e.f;
}

// ---- lcode.c: instructions ----

int Compiler::code(Instruction i) {
    // luaK_code: the instruction and its line (the last token read).
    Proto& f = *fs_->f;
    f.code.push_back(i);
    f.lines.push_back(last_line_);
    return static_cast<int>(f.code.size()) - 1;
}

int Compiler::code_abck(OpCode op, int a, int b, int c, bool k) {
    return code(make_abc(op, a, b, c, k));
}

int Compiler::code_abx(OpCode op, int a, int bx) {
    return code(make_abx(op, a, bx));
}

int Compiler::code_asbx(OpCode op, int a, int sbx) {
    return code(make_asbx(op, a, sbx));
}

int Compiler::code_sj(OpCode op, int sj) {
    return code(make_sj(op, sj));
}

int Compiler::code_extra_arg(int a) {
    return code(make_ax(OpCode::ExtraArg, a));
}

int Compiler::code_k(int reg, int k) {
    // luaK_codek: LOADK, or LOADKX with an extra argument for big indices.
    if (k <= max_bx) {
        return code_abx(OpCode::LoadK, reg, k);
    }
    const int p = code_abx(OpCode::LoadKX, reg, 0);
    code_extra_arg(k);
    return p;
}

void Compiler::fixline(int line) {
    // luaK_fixline
    fs_->f->lines.back() = line;
}

void Compiler::remove_last_instruction() {
    fs_->f->code.pop_back();
    fs_->f->lines.pop_back();
}

Instruction* Compiler::previous_instruction() {
    // previousinstruction: none if a jump may land between the two (Lua
    // returns an invalid instruction instead; a null pointer says it here).
    if (fs_->pc() > fs_->lasttarget) {
        return &fs_->f->code.back();
    }
    return nullptr;
}

void Compiler::nil(int from, int n) {
    // luaK_nil: merge with a LOADNIL just before, when the ranges touch
    // (`local a; local b` is one instruction).
    int l = from + n - 1;
    Instruction* previous = previous_instruction();
    if (previous != nullptr && get_op(*previous) == OpCode::LoadNil) {
        const int pfrom = get_a(*previous);
        const int pl = pfrom + get_b(*previous);
        if ((pfrom <= from && from <= pl + 1) || (from <= pfrom && pfrom <= l + 1)) {
            from = std::min(from, pfrom);
            l = std::max(l, pl);
            *previous = set_b(set_a(*previous, from), l - from);
            return;
        }
    }
    code_abc(OpCode::LoadNil, from, n - 1, 0);
}

// ---- lcode.c: jumps ----

int Compiler::getjump(int pc) {
    const int offset = get_sj(fs_->f->code[index(pc)]);
    return offset == no_jump ? no_jump : pc + 1 + offset;
}

void Compiler::fixjump(int pc, int dest) {
    const int offset = dest - (pc + 1);
    if (offset < -offset_sj || offset > max_sj - offset_sj) {
        error("control structure too long");
        return;
    }
    Instruction& jmp = fs_->f->code[index(pc)];
    jmp = set_sj(jmp, offset);
}

void Compiler::concat(int& l1, int l2) {
    // luaK_concat: append list l2 to list l1.
    if (l2 == no_jump) {
        return;
    }
    if (l1 == no_jump) {
        l1 = l2;
        return;
    }
    int list = l1;
    int next = 0;
    while ((next = getjump(list)) != no_jump) {
        list = next;
    }
    fixjump(list, l2);
}

int Compiler::jump() {
    return code_sj(OpCode::Jmp, no_jump);
}

void Compiler::ret(int first, int nret) {
    // luaK_ret
    OpCode op = OpCode::Return;
    if (nret == 0) {
        op = OpCode::Return0;
    } else if (nret == 1) {
        op = OpCode::Return1;
    }
    code_abc(op, first, nret + 1, 0);
}

int Compiler::condjump(OpCode op, int a, int b, int c, bool k) {
    code_abck(op, a, b, c, k);
    return jump();
}

int Compiler::getlabel() {
    fs_->lasttarget = fs_->pc();
    return fs_->pc();
}

Instruction* Compiler::getjumpcontrol(int pc) {
    Instruction* pi = &fs_->f->code[index(pc)];
    if (pc >= 1 && op_info(get_op(*(pi - 1))).is_test) {
        return pi - 1;
    }
    return pi;
}

bool Compiler::patchtestreg(int node, int reg) {
    // patchtestreg: a TESTSET gets its destination register, or becomes a
    // plain TEST when there is none.
    Instruction* i = getjumpcontrol(node);
    if (get_op(*i) != OpCode::TestSet) {
        return false;
    }
    if (reg != no_reg && reg != get_b(*i)) {
        *i = set_a(*i, reg);
    } else {
        *i = make_abc(OpCode::Test, get_b(*i), 0, 0, get_k(*i));
    }
    return true;
}

void Compiler::removevalues(int list) {
    for (; list != no_jump; list = getjump(list)) {
        patchtestreg(list, no_reg);
    }
}

void Compiler::patchlistaux(int list, int vtarget, int reg, int dtarget) {
    while (list != no_jump) {
        const int next = getjump(list);
        if (patchtestreg(list, reg)) {
            fixjump(list, vtarget);
        } else {
            fixjump(list, dtarget);
        }
        list = next;
    }
}

void Compiler::patchlist(int list, int target) {
    patchlistaux(list, target, no_reg, target);
}

void Compiler::patchtohere(int list) {
    const int here = getlabel();
    patchlist(list, here);
}

// ---- lcode.c: registers ----

void Compiler::checkstack(int n) {
    const int newstack = fs_->freereg + n;
    if (std::cmp_greater(newstack, fs_->f->max_stack_size)) {
        if (newstack >= max_registers) {
            error("function or expression needs too many registers");
            fs_->f->max_stack_size = max_registers;
            return;
        }
        fs_->f->max_stack_size = static_cast<std::uint8_t>(newstack);
    }
}

void Compiler::reserveregs(int n) {
    checkstack(n);
    fs_->freereg += n;
}

void Compiler::freereg(int reg) {
    if (reg >= nvarstack()) {
        fs_->freereg--;
    }
}

void Compiler::freeregs(int r1, int r2) {
    if (r1 > r2) {
        freereg(r1);
        freereg(r2);
    } else {
        freereg(r2);
        freereg(r1);
    }
}

void Compiler::freeexp(const ExpDesc& e) {
    if (e.k == ExpKind::NonReloc) {
        freereg(e.info);
    }
}

void Compiler::freeexps(const ExpDesc& e1, const ExpDesc& e2) {
    const int r1 = e1.k == ExpKind::NonReloc ? e1.info : -1;
    const int r2 = e2.k == ExpKind::NonReloc ? e2.info : -1;
    freeregs(r1, r2);
}

// ---- lcode.c: constants ----

int Compiler::addk(ConstKey key, Value v) {
    // addk: reuse the constant if the cache points at an equal one in this
    // function; the cache is shared by all functions, so it may point
    // elsewhere, and then the constant is added again.
    std::vector<Value>& k = fs_->f->constants;
    const auto it = constant_cache_.find(key);
    if (it != constant_cache_.end()) {
        const int slot = it->second;
        if (std::cmp_less(slot, k.size()) && k[index(slot)].tag() == v.tag() &&
            raw_equal(k[index(slot)], v)) {
            return slot;
        }
    }
    const int slot = static_cast<int>(k.size());
    constant_cache_[key] = slot;
    k.push_back(v);
    return slot;
}

int Compiler::string_k(const String* s) {
    const Value v = Value::string(s);
    return addk(key_of(v), v);
}

int Compiler::int_k(std::int64_t n) {
    const Value v = Value::integer(n);
    return addk(key_of(v), v);
}

int Compiler::number_k(double r) {
    // luaK_numberK: a float with an integer value needs a key of its own,
    // which must not be an integer: r + r * 2^-52.
    const Value v = Value::number(r);
    const auto ik = float_to_int(r);
    if (!ik) {
        return addk(key_of(v), v);
    }
    const double q = std::ldexp(1.0, -52);
    const double k = *ik == 0 ? q : r + (r * q);
    return addk(key_of(Value::number(k)), v);
}

int Compiler::bool_f() {
    return addk(key_of(Value::boolean(false)), Value::boolean(false));
}

int Compiler::bool_t() {
    return addk(key_of(Value::boolean(true)), Value::boolean(true));
}

int Compiler::nil_k() {
    // nil cannot be a key; Lua uses the scanner table itself.
    return addk(ConstKey{.kind = ConstKey::Kind::Nil, .bits = 1}, Value::nil());
}

void Compiler::load_int(int reg, std::int64_t i) {
    // luaK_int
    if (fits_bx(i)) {
        code_asbx(OpCode::LoadI, reg, static_cast<int>(i));
    } else {
        code_k(reg, int_k(i));
    }
}

void Compiler::load_float(int reg, double f) {
    // luaK_float
    const auto fi = float_to_int(f);
    if (fi && fits_bx(*fi)) {
        code_asbx(OpCode::LoadF, reg, static_cast<int>(*fi));
    } else {
        code_k(reg, number_k(f));
    }
}

bool Compiler::tonumeral(const ExpDesc& e, Value* v) {
    if (has_jumps(e)) {
        return false;
    }
    switch (e.k) {
    case ExpKind::KInt:
        if (v != nullptr) {
            *v = Value::integer(e.ival);
        }
        return true;
    case ExpKind::KFlt:
        if (v != nullptr) {
            *v = Value::number(e.nval);
        }
        return true;
    default: return false;
    }
}

bool Compiler::exp2const(const ExpDesc& e, Value& v) {
    // luaK_exp2const
    if (has_jumps(e)) {
        return false;
    }
    switch (e.k) {
    case ExpKind::False: v = Value::boolean(false); return true;
    case ExpKind::True: v = Value::boolean(true); return true;
    case ExpKind::Nil: v = Value::nil(); return true;
    case ExpKind::KStr: v = Value::string(e.strval); return true;
    case ExpKind::Const: v = actvar_[index(e.info)].k; return true;
    default: return tonumeral(e, &v);
    }
}

void Compiler::const2exp(Value v, ExpDesc& e) {
    switch (v.tag()) {
    case Tag::Integer:
        e.k = ExpKind::KInt;
        e.ival = v.as_integer();
        break;
    case Tag::Float:
        e.k = ExpKind::KFlt;
        e.nval = v.as_float();
        break;
    case Tag::False: e.k = ExpKind::False; break;
    case Tag::True: e.k = ExpKind::True; break;
    case Tag::Nil: e.k = ExpKind::Nil; break;
    case Tag::ShortString:
    case Tag::LongString:
        e.k = ExpKind::KStr;
        e.strval = v.as_string();
        break;
    case Tag::CFunction:
    case Tag::LuaClosure:
    case Tag::CClosure:
    case Tag::LightUserdata:
    case Tag::Userdata:
    case Tag::Thread:
    case Tag::Table: break; // never a constant
    }
}

// ---- lcode.c: expressions ----

void Compiler::setreturns(ExpDesc& e, int nresults) {
    // luaK_setreturns: an open call or '...' gives nresults values.
    Instruction& pc = instruction(e);
    if (e.k == ExpKind::Call) {
        pc = set_c(pc, nresults + 1);
    } else {
        pc = set_c(pc, nresults + 1);
        pc = set_a(pc, fs_->freereg);
        reserveregs(1);
    }
}

void Compiler::str2k(ExpDesc& e) {
    e.info = string_k(e.strval);
    e.k = ExpKind::K;
}

void Compiler::setoneret(ExpDesc& e) {
    // luaK_setoneret
    if (e.k == ExpKind::Call) {
        e.k = ExpKind::NonReloc; // the result is where the function was
        e.info = get_a(instruction(e));
    } else if (e.k == ExpKind::Vararg) {
        Instruction& pc = instruction(e);
        pc = set_c(pc, 2);
        e.k = ExpKind::Reloc;
    }
}

void Compiler::dischargevars(ExpDesc& e) {
    // luaK_dischargevars: a variable becomes a value.
    switch (e.k) {
    case ExpKind::Const: const2exp(actvar_[index(e.info)].k, e); break;
    case ExpKind::Local:
        e.info = e.var_ridx;
        e.k = ExpKind::NonReloc;
        break;
    case ExpKind::Upval:
        e.info = code_abc(OpCode::GetUpval, 0, e.info, 0);
        e.k = ExpKind::Reloc;
        break;
    case ExpKind::IndexUp:
        e.info = code_abc(OpCode::GetTabUp, 0, e.ind_t, e.ind_idx);
        e.k = ExpKind::Reloc;
        break;
    case ExpKind::IndexI:
        freereg(e.ind_t);
        e.info = code_abc(OpCode::GetI, 0, e.ind_t, e.ind_idx);
        e.k = ExpKind::Reloc;
        break;
    case ExpKind::IndexStr:
        freereg(e.ind_t);
        e.info = code_abc(OpCode::GetField, 0, e.ind_t, e.ind_idx);
        e.k = ExpKind::Reloc;
        break;
    case ExpKind::Indexed:
        freeregs(e.ind_t, e.ind_idx);
        e.info = code_abc(OpCode::GetTable, 0, e.ind_t, e.ind_idx);
        e.k = ExpKind::Reloc;
        break;
    case ExpKind::Vararg:
    case ExpKind::Call: setoneret(e); break;
    default: break;
    }
}

void Compiler::discharge2reg(ExpDesc& e, int reg) {
    dischargevars(e);
    switch (e.k) {
    case ExpKind::Nil: nil(reg, 1); break;
    case ExpKind::False: code_abc(OpCode::LoadFalse, reg, 0, 0); break;
    case ExpKind::True: code_abc(OpCode::LoadTrue, reg, 0, 0); break;
    case ExpKind::KStr:
        str2k(e);
        code_k(reg, e.info);
        break;
    case ExpKind::K: code_k(reg, e.info); break;
    case ExpKind::KFlt: load_float(reg, e.nval); break;
    case ExpKind::KInt: load_int(reg, e.ival); break;
    case ExpKind::Reloc: {
        Instruction& pc = instruction(e);
        pc = set_a(pc, reg);
        break;
    }
    case ExpKind::NonReloc:
        if (reg != e.info) {
            code_abc(OpCode::Move, reg, e.info, 0);
        }
        break;
    default: return; // a jump: nothing to do
    }
    e.info = reg;
    e.k = ExpKind::NonReloc;
}

void Compiler::discharge2anyreg(ExpDesc& e) {
    if (e.k != ExpKind::NonReloc) {
        reserveregs(1);
        discharge2reg(e, fs_->freereg - 1);
    }
}

int Compiler::code_loadbool(int a, OpCode op) {
    getlabel(); // those instructions may be jump targets
    return code_abc(op, a, 0, 0);
}

bool Compiler::need_value(int list) {
    for (; list != no_jump; list = getjump(list)) {
        if (get_op(*getjumpcontrol(list)) != OpCode::TestSet) {
            return true;
        }
    }
    return false;
}

void Compiler::exp2reg(ExpDesc& e, int reg) {
    // exp2reg: the final value, from every jump list too, in reg.
    discharge2reg(e, reg);
    if (e.k == ExpKind::Jmp) {
        concat(e.t, e.info);
    }
    if (has_jumps(e)) {
        int p_f = no_jump;
        int p_t = no_jump;
        if (need_value(e.t) || need_value(e.f)) {
            const int fj = e.k == ExpKind::Jmp ? no_jump : jump();
            p_f = code_loadbool(reg, OpCode::LFalseSkip);
            p_t = code_loadbool(reg, OpCode::LoadTrue);
            patchtohere(fj);
        }
        const int final = getlabel();
        patchlistaux(e.f, final, reg, p_f);
        patchlistaux(e.t, final, reg, p_t);
    }
    e.f = e.t = no_jump;
    e.info = reg;
    e.k = ExpKind::NonReloc;
}

void Compiler::exp2nextreg(ExpDesc& e) {
    dischargevars(e);
    freeexp(e);
    reserveregs(1);
    exp2reg(e, fs_->freereg - 1);
}

int Compiler::exp2anyreg(ExpDesc& e) {
    dischargevars(e);
    if (e.k == ExpKind::NonReloc) {
        if (!has_jumps(e)) {
            return e.info;
        }
        if (e.info >= nvarstack()) {
            exp2reg(e, e.info);
            return e.info;
        }
        // A local with jumps cannot take the jumps' values: next register.
    }
    exp2nextreg(e);
    return e.info;
}

void Compiler::exp2anyregup(ExpDesc& e) {
    if (e.k != ExpKind::Upval || has_jumps(e)) {
        exp2anyreg(e);
    }
}

void Compiler::exp2val(ExpDesc& e) {
    if (e.k == ExpKind::Jmp || has_jumps(e)) {
        exp2anyreg(e);
    } else {
        dischargevars(e);
    }
}

bool Compiler::exp2k(ExpDesc& e) {
    // luaK_exp2K: a constant with an index that fits an operand.
    if (has_jumps(e)) {
        return false;
    }
    int info = 0;
    switch (e.k) {
    case ExpKind::True: info = bool_t(); break;
    case ExpKind::False: info = bool_f(); break;
    case ExpKind::Nil: info = nil_k(); break;
    case ExpKind::KInt: info = int_k(e.ival); break;
    case ExpKind::KFlt: info = number_k(e.nval); break;
    case ExpKind::KStr: info = string_k(e.strval); break;
    case ExpKind::K: info = e.info; break;
    default: return false;
    }
    if (info <= max_index_rk) {
        e.k = ExpKind::K;
        e.info = info;
        return true;
    }
    return false;
}

bool Compiler::exp2rk(ExpDesc& e) {
    if (exp2k(e)) {
        return true;
    }
    exp2anyreg(e);
    return false;
}

void Compiler::code_abrk(OpCode op, int a, int b, ExpDesc& ec) {
    const bool k = exp2rk(ec);
    code_abck(op, a, b, ec.info, k);
}

void Compiler::storevar(const ExpDesc& var, ExpDesc& ex) {
    // luaK_storevar
    switch (var.k) {
    case ExpKind::Local:
        freeexp(ex);
        exp2reg(ex, var.var_ridx);
        return;
    case ExpKind::Upval: {
        const int e = exp2anyreg(ex);
        code_abc(OpCode::SetUpval, e, var.info, 0);
        break;
    }
    case ExpKind::IndexUp: code_abrk(OpCode::SetTabUp, var.ind_t, var.ind_idx, ex); break;
    case ExpKind::IndexI: code_abrk(OpCode::SetI, var.ind_t, var.ind_idx, ex); break;
    case ExpKind::IndexStr: code_abrk(OpCode::SetField, var.ind_t, var.ind_idx, ex); break;
    case ExpKind::Indexed: code_abrk(OpCode::SetTable, var.ind_t, var.ind_idx, ex); break;
    default: break; // not a variable
    }
    freeexp(ex);
}

void Compiler::self(ExpDesc& e, ExpDesc& key) {
    // luaK_self: e:key becomes e.key(e, ...
    exp2anyreg(e);
    const int ereg = e.info;
    freeexp(e);
    e.info = fs_->freereg;
    e.k = ExpKind::NonReloc;
    reserveregs(2);
    code_abrk(OpCode::Self, e.info, ereg, key);
    freeexp(key);
}

void Compiler::negatecondition(const ExpDesc& e) {
    Instruction* pc = getjumpcontrol(e.info);
    *pc = set_k(*pc, !get_k(*pc));
}

int Compiler::jumponcond(ExpDesc& e, bool cond) {
    // jumponcond: `not x` tests x the other way, without the NOT.
    if (e.k == ExpKind::Reloc) {
        const Instruction ie = instruction(e);
        if (get_op(ie) == OpCode::Not) {
            remove_last_instruction();
            return condjump(OpCode::Test, get_b(ie), 0, 0, !cond);
        }
    }
    discharge2anyreg(e);
    freeexp(e);
    return condjump(OpCode::TestSet, no_reg, e.info, 0, cond);
}

void Compiler::goiftrue(ExpDesc& e) {
    // luaK_goiftrue: go on when e is true, jump away otherwise.
    dischargevars(e);
    int pc = no_jump;
    switch (e.k) {
    case ExpKind::Jmp:
        negatecondition(e);
        pc = e.info;
        break;
    case ExpKind::K:
    case ExpKind::KFlt:
    case ExpKind::KInt:
    case ExpKind::KStr:
    case ExpKind::True: pc = no_jump; break; // always true
    default: pc = jumponcond(e, false); break;
    }
    concat(e.f, pc);
    patchtohere(e.t);
    e.t = no_jump;
}

void Compiler::goiffalse(ExpDesc& e) {
    // luaK_goiffalse
    dischargevars(e);
    int pc = no_jump;
    switch (e.k) {
    case ExpKind::Jmp: pc = e.info; break;
    case ExpKind::Nil:
    case ExpKind::False: pc = no_jump; break; // always false
    default: pc = jumponcond(e, true); break;
    }
    concat(e.t, pc);
    patchtohere(e.f);
    e.f = no_jump;
}

void Compiler::codenot(ExpDesc& e) {
    switch (e.k) {
    case ExpKind::Nil:
    case ExpKind::False: e.k = ExpKind::True; break;
    case ExpKind::K:
    case ExpKind::KFlt:
    case ExpKind::KInt:
    case ExpKind::KStr:
    case ExpKind::True: e.k = ExpKind::False; break;
    case ExpKind::Jmp: negatecondition(e); break;
    case ExpKind::Reloc:
    case ExpKind::NonReloc:
        discharge2anyreg(e);
        freeexp(e);
        e.info = code_abc(OpCode::Not, 0, e.info, 0);
        e.k = ExpKind::Reloc;
        break;
    default: break; // cannot happen
    }
    std::swap(e.f, e.t);
    removevalues(e.f);
    removevalues(e.t);
}

bool Compiler::is_kstr(const ExpDesc& e) const {
    return e.k == ExpKind::K && !has_jumps(e) && e.info <= max_b &&
           fs_->f->constants[index(e.info)].tag() == Tag::ShortString;
}

bool is_kint(const ExpDesc& e) {
    return e.k == ExpKind::KInt && !has_jumps(e);
}

bool is_cint(const ExpDesc& e) {
    return is_kint(e) && static_cast<std::uint64_t>(e.ival) <= static_cast<std::uint64_t>(max_c);
}

bool is_scint(const ExpDesc& e) {
    return is_kint(e) && fits_c(e.ival);
}

// isSCnumber: an integer, or a float with an integer value, that fits sC.
bool is_scnumber(const ExpDesc& e, int& pi, bool& isfloat) {
    std::int64_t i = 0;
    if (e.k == ExpKind::KInt) {
        i = e.ival;
    } else if (e.k == ExpKind::KFlt) {
        const auto fi = float_to_int(e.nval);
        if (!fi) {
            return false;
        }
        i = *fi;
        isfloat = true;
    } else {
        return false;
    }
    if (!has_jumps(e) && fits_c(i)) {
        pi = to_sc(static_cast<int>(i));
        return true;
    }
    return false;
}

void Compiler::indexed(ExpDesc& t, ExpDesc& k) {
    // luaK_indexed: t[k], choosing the form by the key.
    if (k.k == ExpKind::KStr) {
        str2k(k);
    }
    if (t.k == ExpKind::Upval && !is_kstr(k)) {
        exp2anyreg(t); // an upvalue can only be indexed by a short string constant
    }
    if (t.k == ExpKind::Upval) {
        t.ind_t = t.info;
        t.ind_idx = k.info;
        t.k = ExpKind::IndexUp;
        return;
    }
    t.ind_t = t.k == ExpKind::Local ? t.var_ridx : t.info;
    if (is_kstr(k)) {
        t.ind_idx = k.info;
        t.k = ExpKind::IndexStr;
    } else if (is_cint(k)) {
        t.ind_idx = static_cast<int>(k.ival);
        t.k = ExpKind::IndexI;
    } else {
        t.ind_idx = exp2anyreg(k);
        t.k = ExpKind::Indexed;
    }
}

// validop: folding must not raise an error, so bitwise operations need
// integer operands and divisions a nonzero divisor.
bool foldable(BinOpr op, Value v1, Value v2) {
    switch (op) {
    case BinOpr::BAnd:
    case BinOpr::BOr:
    case BinOpr::BXor:
    case BinOpr::Shl:
    case BinOpr::Shr: return to_integer(v1).has_value() && to_integer(v2).has_value();
    case BinOpr::Div:
    case BinOpr::IDiv:
    case BinOpr::Mod: return to_float(v2).value_or(0) != 0;
    default: return true;
    }
}

ArithResult fold(BinOpr op, Value a, Value b) {
    switch (op) {
    case BinOpr::Add: return add(a, b);
    case BinOpr::Sub: return subtract(a, b);
    case BinOpr::Mul: return multiply(a, b);
    case BinOpr::Mod: return modulo(a, b);
    case BinOpr::Pow: return power(a, b);
    case BinOpr::Div: return divide(a, b);
    case BinOpr::IDiv: return floor_divide(a, b);
    case BinOpr::BAnd: return bit_and(a, b);
    case BinOpr::BOr: return bit_or(a, b);
    case BinOpr::BXor: return bit_xor(a, b);
    case BinOpr::Shl: return shift_left(a, b);
    case BinOpr::Shr: return shift_right(a, b);
    default: break;
    }
    return std::unexpected(RuntimeError{}); // not foldable
}

// The end of constfolding: the result goes into e, except NaN and zero
// floats (to keep -0.0 apart from 0.0).
bool fold_into(ExpDesc& e, const ArithResult& result) {
    if (!result) {
        return false;
    }
    if (result->is_integer()) {
        e.k = ExpKind::KInt;
        e.ival = result->as_integer();
        return true;
    }
    const double n = result->as_float();
    if (std::isnan(n) || n == 0) {
        return false;
    }
    e.k = ExpKind::KFlt;
    e.nval = n;
    return true;
}

bool Compiler::constfolding(BinOpr op, ExpDesc& e1, const ExpDesc& e2) {
    Value v1;
    Value v2;
    if (!tonumeral(e1, &v1) || !tonumeral(e2, &v2) || !foldable(op, v1, v2)) {
        return false;
    }
    return fold_into(e1, fold(op, v1, v2));
}

bool Compiler::constfolding_unary(UnOpr op, ExpDesc& e) {
    // luaK_prefix folds - and ~ with an integer 0 as the fake second
    // operand, which only matters for validop.
    Value v;
    if (!tonumeral(e, &v)) {
        return false;
    }
    if (op == UnOpr::BNot) {
        if (!to_integer(v)) {
            return false;
        }
        return fold_into(e, bit_not(v));
    }
    return fold_into(e, negate(v));
}

void Compiler::codeunexpval(OpCode op, ExpDesc& e, int line) {
    const int r = exp2anyreg(e);
    freeexp(e);
    e.info = code_abc(op, 0, r, 0);
    e.k = ExpKind::Reloc;
    fixline(line);
}

void Compiler::finishbinexpval(ExpDesc& e1, ExpDesc& e2, OpCode op, int v2, bool flip, int line,
                               OpCode mmop, int event) {
    const int v1 = exp2anyreg(e1);
    const int pc = code_abck(op, 0, v1, v2, false);
    freeexps(e1, e2);
    e1.info = pc;
    e1.k = ExpKind::Reloc;
    fixline(line);
    code_abck(mmop, v1, v2, event, flip); // the metamethod fallback
    fixline(line);
}

void Compiler::codebinexpval(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line) {
    const OpCode op = opcode_from(opr, BinOpr::Add, OpCode::Add);
    const int v2 = exp2anyreg(e2);
    finishbinexpval(e1, e2, op, v2, false, line, OpCode::MmBin, event_of(opr));
}

void Compiler::codebini(OpCode op, ExpDesc& e1, ExpDesc& e2, bool flip, int line, int event) {
    const int v2 = to_sc(static_cast<int>(e2.ival));
    finishbinexpval(e1, e2, op, v2, flip, line, OpCode::MmBinI, event);
}

void Compiler::codebink(BinOpr opr, ExpDesc& e1, ExpDesc& e2, bool flip, int line) {
    const int v2 = e2.info;
    const OpCode op = opcode_from(opr, BinOpr::Add, OpCode::AddK);
    finishbinexpval(e1, e2, op, v2, flip, line, OpCode::MmBinK, event_of(opr));
}

bool Compiler::finishbinexpneg(ExpDesc& e1, ExpDesc& e2, OpCode op, int line, int event) {
    // a - i becomes a + -i (and a << i becomes a >> -i); the metamethod
    // still gets i.
    if (!is_kint(e2)) {
        return false;
    }
    const std::int64_t i2 = e2.ival;
    if (!(fits_c(i2) && fits_c(-i2))) {
        return false;
    }
    const int v2 = static_cast<int>(i2);
    finishbinexpval(e1, e2, op, to_sc(-v2), false, line, OpCode::MmBinI, event);
    Instruction& mm = fs_->f->code.back();
    mm = set_b(mm, to_sc(v2));
    return true;
}

void Compiler::codebinnok(BinOpr opr, ExpDesc& e1, ExpDesc& e2, bool flip, int line) {
    if (flip) {
        std::swap(e1, e2); // back to the original order
    }
    codebinexpval(opr, e1, e2, line);
}

void Compiler::codearith(BinOpr opr, ExpDesc& e1, ExpDesc& e2, bool flip, int line) {
    if (tonumeral(e2, nullptr) && exp2k(e2)) {
        codebink(opr, e1, e2, flip, line);
    } else {
        codebinnok(opr, e1, e2, flip, line);
    }
}

void Compiler::codecommutative(BinOpr op, ExpDesc& e1, ExpDesc& e2, int line) {
    // + and *: a numeric constant goes second, to use an immediate or K form.
    bool flip = false;
    if (tonumeral(e1, nullptr)) {
        std::swap(e1, e2);
        flip = true;
    }
    if (op == BinOpr::Add && is_scint(e2)) {
        codebini(OpCode::AddI, e1, e2, flip, line, event_of(BinOpr::Add));
    } else {
        codearith(op, e1, e2, flip, line);
    }
}

void Compiler::codebitwise(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line) {
    bool flip = false;
    if (e1.k == ExpKind::KInt) {
        std::swap(e1, e2);
        flip = true;
    }
    if (e2.k == ExpKind::KInt && exp2k(e2)) {
        codebink(opr, e1, e2, flip, line);
    } else {
        codebinnok(opr, e1, e2, flip, line);
    }
}

void Compiler::codeorder(BinOpr opr, ExpDesc& e1, ExpDesc& e2) {
    // < and <=, with an immediate operand when one side is a small number.
    int r1 = 0;
    int r2 = 0;
    int im = 0;
    bool isfloat = false;
    OpCode op = OpCode::Lt;
    if (is_scnumber(e2, im, isfloat)) {
        r1 = exp2anyreg(e1);
        r2 = im;
        op = opcode_from(opr, BinOpr::Lt, OpCode::LtI);
    } else if (is_scnumber(e1, im, isfloat)) {
        // a < b is b > a, and a <= b is b >= a.
        r1 = exp2anyreg(e2);
        r2 = im;
        op = opcode_from(opr, BinOpr::Lt, OpCode::GtI);
    } else {
        r1 = exp2anyreg(e1);
        r2 = exp2anyreg(e2);
        op = opcode_from(opr, BinOpr::Lt, OpCode::Lt);
    }
    freeexps(e1, e2);
    e1.info = condjump(op, r1, r2, isfloat ? 1 : 0, true);
    e1.k = ExpKind::Jmp;
}

void Compiler::codeeq(BinOpr opr, ExpDesc& e1, ExpDesc& e2) {
    // == and ~=: e1 was made a register or a constant by infix.
    int r2 = 0;
    int im = 0;
    bool isfloat = false;
    OpCode op = OpCode::Eq;
    if (e1.k != ExpKind::NonReloc) {
        std::swap(e1, e2); // the constant goes second
    }
    const int r1 = exp2anyreg(e1);
    if (is_scnumber(e2, im, isfloat)) {
        op = OpCode::EqI;
        r2 = im;
    } else if (exp2rk(e2)) {
        op = OpCode::EqK;
        r2 = e2.info;
    } else {
        op = OpCode::Eq;
        r2 = exp2anyreg(e2);
    }
    freeexps(e1, e2);
    e1.info = condjump(op, r1, r2, isfloat ? 1 : 0, opr == BinOpr::Eq);
    e1.k = ExpKind::Jmp;
}

void Compiler::prefix(UnOpr op, ExpDesc& e, int line) {
    // luaK_prefix
    dischargevars(e);
    switch (op) {
    case UnOpr::Minus:
    case UnOpr::BNot:
        if (constfolding_unary(op, e)) {
            break;
        }
        codeunexpval(op == UnOpr::Minus ? OpCode::Unm : OpCode::BNot, e, line);
        break;
    case UnOpr::Len: codeunexpval(OpCode::Len, e, line); break;
    case UnOpr::Not: codenot(e); break;
    }
}

void Compiler::infix(BinOpr op, ExpDesc& v) {
    // luaK_infix: the first operand, before the second is compiled.
    dischargevars(v);
    switch (op) {
    case BinOpr::And: goiftrue(v); break;
    case BinOpr::Or: goiffalse(v); break;
    case BinOpr::Concat: exp2nextreg(v); break; // operands must be consecutive
    case BinOpr::Add:
    case BinOpr::Sub:
    case BinOpr::Mul:
    case BinOpr::Div:
    case BinOpr::IDiv:
    case BinOpr::Mod:
    case BinOpr::Pow:
    case BinOpr::BAnd:
    case BinOpr::BOr:
    case BinOpr::BXor:
    case BinOpr::Shl:
    case BinOpr::Shr:
        if (!tonumeral(v, nullptr)) {
            exp2anyreg(v);
        }
        // A numeral stays, to be folded or used as an immediate operand.
        break;
    case BinOpr::Eq:
    case BinOpr::Ne:
        if (!tonumeral(v, nullptr)) {
            exp2rk(v);
        }
        break;
    case BinOpr::Lt:
    case BinOpr::Le:
    case BinOpr::Gt:
    case BinOpr::Ge: {
        int dummy = 0;
        bool dummy2 = false;
        if (!is_scnumber(v, dummy, dummy2)) {
            exp2anyreg(v);
        }
        break;
    }
    }
}

void Compiler::codeconcat(ExpDesc& e1, ExpDesc& e2, int line) {
    // a .. (b .. c) is one CONCAT of three values.
    Instruction* ie2 = previous_instruction();
    if (ie2 != nullptr && get_op(*ie2) == OpCode::Concat) {
        const int n = get_b(*ie2);
        freeexp(e2);
        *ie2 = set_b(set_a(*ie2, e1.info), n + 1);
    } else {
        code_abc(OpCode::Concat, e1.info, 2, 0);
        freeexp(e2);
        fixline(line);
    }
}

void Compiler::posfix(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line) {
    // luaK_posfix: the operation, once both operands are compiled.
    dischargevars(e2);
    if (opr <= BinOpr::Shr && constfolding(opr, e1, e2)) {
        return;
    }
    switch (opr) {
    case BinOpr::And:
        concat(e2.f, e1.f);
        e1 = e2;
        break;
    case BinOpr::Or:
        concat(e2.t, e1.t);
        e1 = e2;
        break;
    case BinOpr::Concat:
        exp2nextreg(e2);
        codeconcat(e1, e2, line);
        break;
    case BinOpr::Add:
    case BinOpr::Mul: codecommutative(opr, e1, e2, line); break;
    case BinOpr::Sub:
        if (finishbinexpneg(e1, e2, OpCode::AddI, line, event_of(BinOpr::Sub))) {
            break;
        }
        codearith(opr, e1, e2, false, line);
        break;
    case BinOpr::Div:
    case BinOpr::IDiv:
    case BinOpr::Mod:
    case BinOpr::Pow: codearith(opr, e1, e2, false, line); break;
    case BinOpr::BAnd:
    case BinOpr::BOr:
    case BinOpr::BXor: codebitwise(opr, e1, e2, line); break;
    case BinOpr::Shl:
        if (is_scint(e1)) {
            std::swap(e1, e2);
            codebini(OpCode::ShlI, e1, e2, true, line, tm_shl); // i << r
        } else if (finishbinexpneg(e1, e2, OpCode::ShrI, line, tm_shl)) {
            // r << i coded as r >> -i
        } else {
            codebinexpval(opr, e1, e2, line);
        }
        break;
    case BinOpr::Shr:
        if (is_scint(e2)) {
            codebini(OpCode::ShrI, e1, e2, false, line, tm_shr);
        } else {
            codebinexpval(opr, e1, e2, line);
        }
        break;
    case BinOpr::Eq:
    case BinOpr::Ne: codeeq(opr, e1, e2); break;
    case BinOpr::Gt:
    case BinOpr::Ge:
        // a > b is b < a, and a >= b is b <= a.
        std::swap(e1, e2);
        codeorder(opr == BinOpr::Gt ? BinOpr::Lt : BinOpr::Le, e1, e2);
        break;
    case BinOpr::Lt:
    case BinOpr::Le: codeorder(opr, e1, e2); break;
    }
}

void Compiler::settablesize(int pc, int ra, int asize, int hsize) {
    // luaK_settablesize
    Instruction& inst = fs_->f->code[index(pc)];
    const int rb = hsize != 0 ? std::bit_width(static_cast<unsigned>(hsize - 1)) + 1 : 0;
    const int extra = asize / (max_c + 1);
    const int rc = asize % (max_c + 1);
    inst = make_abc(OpCode::NewTable, ra, rb, rc, extra > 0);
    fs_->f->code[index(pc + 1)] = make_ax(OpCode::ExtraArg, extra);
}

void Compiler::setlist(int base, int nelems, int tostore) {
    // luaK_setlist
    if (tostore == multiple_results) {
        tostore = 0;
    }
    if (nelems <= max_c) {
        code_abc(OpCode::SetList, base, tostore, nelems);
    } else {
        const int extra = nelems / (max_c + 1);
        nelems %= (max_c + 1);
        code_abck(OpCode::SetList, base, tostore, nelems, true);
        code_extra_arg(extra);
    }
    fs_->freereg = base + 1;
}

// finaltarget: where a chain of jumps ends.
int finaltarget(const std::vector<Instruction>& code, int i) {
    for (int count = 0; count < 100; ++count) {
        const Instruction pc = code[index(i)];
        if (get_op(pc) != OpCode::Jmp) {
            break;
        }
        i += get_sj(pc) + 1;
    }
    return i;
}

void Compiler::finish() {
    // luaK_finish: returns of functions that close upvalues or have varargs
    // say so; jumps to jumps go straight to the end.
    Proto& p = *fs_->f;
    for (int i = 0; i < fs_->pc(); ++i) {
        Instruction& pc = p.code[index(i)];
        switch (get_op(pc)) {
        case OpCode::Return0:
        case OpCode::Return1:
            if (!(fs_->needclose || p.is_vararg)) {
                break;
            }
            pc = (pc & ~Instruction{0x7f}) | static_cast<Instruction>(OpCode::Return);
            [[fallthrough]];
        case OpCode::Return:
        case OpCode::TailCall:
            if (fs_->needclose) {
                pc = set_k(pc, true);
            }
            if (p.is_vararg) {
                pc = set_c(pc, p.parameter_count + 1);
            }
            break;
        case OpCode::Jmp: fixjump(i, finaltarget(p.code, i)); break;
        default: break;
        }
    }
}

// ---- lparser.c: variables ----

int Compiler::new_localvar(const String* name) {
    // new_localvar: entries past active_count_ are reused.
    const VarDesc var{
        .k = Value::nil(),
        .kind = VarKind::Regular,
        .ridx = 0,
        .pidx = 0,
        .name = name,
    };
    if (std::cmp_less(active_count_, actvar_.size())) {
        actvar_[index(active_count_)] = var;
    } else {
        actvar_.push_back(var);
    }
    ++active_count_;
    return active_count_ - 1 - fs_->firstlocal;
}

VarDesc& Compiler::getlocalvardesc(const FuncState* fs, int vidx) {
    return actvar_[index(fs->firstlocal + vidx)];
}

int Compiler::reglevel(const FuncState* fs, int nvar) {
    // reglevel: the register after the last variable below nvar that has
    // one (compile-time constants have none).
    while (nvar-- > 0) {
        const VarDesc& vd = getlocalvardesc(fs, nvar);
        if (vd.kind != VarKind::CompileTimeConst) {
            return vd.ridx + 1;
        }
    }
    return 0;
}

Proto::LocalInfo* Compiler::localdebuginfo(const FuncState* fs, int vidx) {
    const VarDesc& vd = getlocalvardesc(fs, vidx);
    if (vd.kind == VarKind::CompileTimeConst) {
        return nullptr;
    }
    return &fs->f->locals[index(vd.pidx)];
}

void Compiler::init_var(const FuncState* fs, ExpDesc& e, int vidx) {
    e.f = e.t = no_jump;
    e.k = ExpKind::Local;
    e.var_vidx = vidx;
    e.var_ridx = getlocalvardesc(fs, vidx).ridx;
}

void Compiler::adjustlocalvars(int nvars) {
    // adjustlocalvars: the last nvars variables become active, each in the
    // next register, with a debug entry starting here.
    int level = nvarstack();
    for (int i = 0; i < nvars; ++i) {
        const int vidx = fs_->nactvar++;
        VarDesc& var = getlocalvardesc(fs_, vidx);
        var.ridx = level++;
        fs_->f->locals.push_back(Proto::LocalInfo{.name = var.name, .start_pc = fs_->pc()});
        var.pidx = static_cast<int>(fs_->f->locals.size()) - 1;
    }
}

void Compiler::removevars(int tolevel) {
    // removevars: the variables go out of scope; their debug entries end.
    const int removed = fs_->nactvar - tolevel;
    while (fs_->nactvar > tolevel) {
        if (Proto::LocalInfo* var = localdebuginfo(fs_, --fs_->nactvar)) {
            var->end_pc = fs_->pc();
        }
    }
    active_count_ -= removed;
}

int Compiler::searchupvalue(const FuncState* fs, const String* name) {
    const auto& upvalues = fs->f->upvalues;
    for (std::size_t i = 0; i < upvalues.size(); ++i) {
        if (upvalues[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Compiler::newupvalue(FuncState* fs, const String* name, const ExpDesc& v) {
    auto& upvalues = fs->f->upvalues;
    if (static_cast<int>(upvalues.size()) + 1 > max_upvalues) {
        const std::string where = fs->f->line_defined == 0
                                      ? std::string{"main function"}
                                      : "function at line " + std::to_string(fs->f->line_defined);
        error("too many upvalues (limit is 255) in " + where);
    }
    Proto::UpvalueInfo up{.name = name};
    if (v.k == ExpKind::Local) {
        up.in_stack = true;
        up.index = static_cast<std::uint8_t>(v.var_ridx);
        up.kind = static_cast<std::uint8_t>(getlocalvardesc(fs->prev, v.var_vidx).kind);
    } else {
        up.in_stack = false;
        up.index = static_cast<std::uint8_t>(v.info);
        up.kind = fs->prev->f->upvalues[index(v.info)].kind;
    }
    upvalues.push_back(up);
    return static_cast<int>(upvalues.size()) - 1;
}

bool Compiler::searchvar(const FuncState* fs, const String* n, ExpDesc& var) {
    for (int i = fs->nactvar - 1; i >= 0; --i) {
        const VarDesc& vd = getlocalvardesc(fs, i);
        if (vd.name == n) {
            if (vd.kind == VarKind::CompileTimeConst) {
                init_exp(var, ExpKind::Const, fs->firstlocal + i);
            } else {
                init_var(fs, var, i);
            }
            return true;
        }
    }
    return false;
}

void Compiler::markupval(FuncState* fs, int level) {
    // markupval: the block that declared the variable must close it.
    BlockCnt* bl = fs->bl; // NOLINT(misc-const-correctness): written below
    while (bl->nactvar > level) {
        bl = bl->previous;
    }
    bl->upval = true;
    fs->needclose = true;
}

void Compiler::marktobeclosed(FuncState* fs) {
    BlockCnt* bl = fs->bl;
    bl->upval = true;
    bl->insidetbc = true;
    fs->needclose = true;
}

void Compiler::singlevaraux(FuncState* fs, const String* n, ExpDesc& var, bool base) {
    // singlevaraux: a local here; else an upvalue here, made from a local
    // or upvalue of an enclosing function; else a global (Void).
    if (fs == nullptr) {
        init_exp(var, ExpKind::Void, 0);
        return;
    }
    if (searchvar(fs, n, var)) {
        if (var.k == ExpKind::Local && !base) {
            markupval(fs, var.var_vidx);
        }
        return;
    }
    int idx = searchupvalue(fs, n);
    if (idx < 0) {
        singlevaraux(fs->prev, n, var, false);
        if (var.k == ExpKind::Local || var.k == ExpKind::Upval) {
            idx = newupvalue(fs, n, var);
        } else {
            return; // a global or a compile-time constant
        }
    }
    init_exp(var, ExpKind::Upval, idx);
}

void Compiler::singlevar(const String* name, ExpDesc& var) {
    singlevaraux(fs_, name, var, true);
    if (var.k == ExpKind::Void) { // a global: _ENV[name]
        ExpDesc key;
        singlevaraux(fs_, env_name_, var, true);
        exp2anyregup(var);
        codestring(key, name);
        indexed(var, key);
    }
}

void Compiler::adjust_assign(int nvars, int nexps, ExpDesc& e) {
    // adjust_assign: nexps values become nvars (E3.1).
    const int needed = nvars - nexps;
    if (has_multiple_results(e.k)) {
        setreturns(e, std::max(needed + 1, 0));
    } else {
        if (e.k != ExpKind::Void) {
            exp2nextreg(e);
        }
        if (needed > 0) {
            nil(fs_->freereg, needed);
        }
    }
    if (needed > 0) {
        reserveregs(needed);
    } else {
        fs_->freereg += needed;
    }
}

// ---- lparser.c: labels and blocks ----

void Compiler::solvegoto(std::size_t g, const LabelDesc& label) {
    patchlist(gotos_[g].pc, label.pc);
    gotos_.erase(gotos_.begin() + static_cast<std::ptrdiff_t>(g));
}

int Compiler::findlabel(const String* name) const {
    for (int i = fs_->firstlabel; std::cmp_less(i, labels_.size()); ++i) {
        if (labels_[index(i)].name == name) {
            return i;
        }
    }
    return -1;
}

int Compiler::newlabelentry(std::vector<LabelDesc>& list, const String* name, int line, int pc) {
    list.push_back(
        LabelDesc{.name = name, .pc = pc, .line = line, .nactvar = fs_->nactvar, .close = false});
    return static_cast<int>(list.size()) - 1;
}

bool Compiler::solvegotos(const LabelDesc& label) {
    // solvegotos: the pending gotos of this block that jump to the label.
    auto i = static_cast<std::size_t>(fs_->bl->firstgoto);
    bool needsclose = false;
    while (i < gotos_.size()) {
        if (gotos_[i].name == label.name) {
            needsclose = needsclose || gotos_[i].close;
            solvegoto(i, label);
        } else {
            ++i;
        }
    }
    return needsclose;
}

bool Compiler::createlabel(const String* name, int line, bool last) {
    // createlabel: a label at the end of its block is outside the block's
    // locals. Returns whether it closes upvalues.
    const int l = newlabelentry(labels_, name, line, getlabel());
    if (last) {
        labels_[index(l)].nactvar = fs_->bl->nactvar;
    }
    const LabelDesc label = labels_[index(l)];
    if (solvegotos(label)) {
        code_abc(OpCode::Close, nvarstack(), 0, 0);
        return true;
    }
    return false;
}

void Compiler::movegotosout(const BlockCnt& bl) {
    for (auto i = static_cast<std::size_t>(bl.firstgoto); i < gotos_.size(); ++i) {
        LabelDesc& gt = gotos_[i];
        if (reglevel(fs_, gt.nactvar) > reglevel(fs_, bl.nactvar)) {
            gt.close = gt.close || bl.upval;
        }
        gt.nactvar = bl.nactvar;
    }
}

void Compiler::enterblock(BlockCnt& bl, bool isloop) {
    bl.isloop = isloop;
    bl.nactvar = fs_->nactvar;
    bl.firstlabel = static_cast<int>(labels_.size());
    bl.firstgoto = static_cast<int>(gotos_.size());
    bl.upval = false;
    bl.insidetbc = fs_->bl != nullptr && fs_->bl->insidetbc;
    bl.previous = fs_->bl;
    fs_->bl = &bl;
}

void Compiler::leaveblock() {
    const BlockCnt* bl = fs_->bl;
    bool hasclose = false;
    const int stklevel = reglevel(fs_, bl->nactvar);
    removevars(bl->nactvar);
    if (bl->isloop) {
        hasclose = createlabel(break_name_, 0, false); // where breaks go
    }
    if (!hasclose && bl->previous != nullptr && bl->upval) {
        code_abc(OpCode::Close, stklevel, 0, 0);
    }
    fs_->freereg = stklevel;
    labels_.resize(static_cast<std::size_t>(bl->firstlabel));
    fs_->bl = bl->previous;
    if (bl->previous != nullptr) {
        movegotosout(*bl);
    }
    // (A pending goto at the function's end is an error the parser reports.)
}

// ---- lparser.c: functions ----

Proto* Compiler::addprototype() {
    Proto* p = heap_.new_proto();
    fs_->f->protos.push_back(p);
    return p;
}

void Compiler::codeclosure(ExpDesc& v) {
    // codeclosure: CLOSURE in the enclosing function, in its last register.
    FuncState* child = fs_;
    fs_ = child->prev;
    init_exp(v, ExpKind::Reloc,
             code_abx(OpCode::Closure, 0, static_cast<int>(fs_->f->protos.size()) - 1));
    exp2nextreg(v);
    fs_ = child;
}

void Compiler::open_func(FuncState& fs, BlockCnt& bl) {
    fs.prev = fs_;
    fs_ = &fs;
    fs.firstlocal = active_count_;
    fs.firstlabel = static_cast<int>(labels_.size());
    fs.bl = nullptr;
    fs.f->source = source_;
    fs.f->max_stack_size = 2;
    enterblock(bl, false);
}

void Compiler::close_func() {
    Proto& f = *fs_->f;
    ret(nvarstack(), 0); // the final return
    leaveblock();
    finish();
    f.code.shrink_to_fit();
    f.lines.shrink_to_fit();
    f.constants.shrink_to_fit();
    f.protos.shrink_to_fit();
    f.locals.shrink_to_fit();
    f.upvalues.shrink_to_fit();
    f.counted_bytes = f.vector_bytes();
    heap_.count_bytes(static_cast<std::ptrdiff_t>(f.counted_bytes));
    fs_ = fs_->prev;
}

void Compiler::setvararg(int nparams) {
    fs_->f->is_vararg = true;
    code_abc(OpCode::VarArgPrep, nparams, 0, 0);
}

void Compiler::body(ExpDesc& e, FunctionId id, int line) {
    // body: '(' parlist ')' block END
    const Function& function = ast_.function(id);
    FuncState new_fs;
    BlockCnt bl;
    new_fs.f = addprototype();
    new_fs.f->line_defined = line;
    open_func(new_fs, bl);
    // parlist (a method's `self` is its first parameter)
    const auto parameters = ast_.local_names(function.parameters);
    for (const LocalName& parameter : parameters) {
        new_localvar(string_of(parameter.name));
    }
    adjustlocalvars(static_cast<int>(parameters.size()));
    new_fs.f->parameter_count = static_cast<std::uint8_t>(fs_->nactvar);
    if (function.is_vararg) {
        last_line_ = function.vararg_line; // '...'
        setvararg(new_fs.f->parameter_count);
    }
    reserveregs(fs_->nactvar);
    statlist(ast_.statements(function.body));
    new_fs.f->last_line_defined = function.end_line;
    last_line_ = function.end_line; // 'end'
    codeclosure(e);
    close_func();
}

// ---- lparser.c: expressions ----

int Compiler::first_line(ExprId id) const {
    // The line of an expression's first token.
    return std::visit(Overloaded{
                          [&](const BinaryExpr& e) { return first_line(e.left); },
                          [&](const CallExpr& e) { return first_line(e.function); },
                          [&](const MethodCallExpr& e) { return first_line(e.object); },
                          [&](const IndexExpr& e) { return first_line(e.object); },
                          [&](const auto&) { return ast_.line(id); },
                      },
                      ast_.expression(id));
}

int Compiler::explist(IdRange<ExprId> list, ExpDesc& v) {
    const auto expressions = ast_.expressions(list);
    expr(expressions[0], v);
    for (std::size_t i = 1; i < expressions.size(); ++i) {
        exp2nextreg(v);
        expr(expressions[i], v);
    }
    return static_cast<int>(expressions.size());
}

void Compiler::funcargs(ExpDesc& f, IdRange<ExprId> arguments, int line, int end_line) {
    ExpDesc args;
    last_line_ = line; // '(' or the single argument's first token
    if (arguments.count == 0) {
        args.k = ExpKind::Void;
    } else {
        explist(arguments, args);
        if (has_multiple_results(args.k)) {
            setmultret(args);
        }
    }
    last_line_ = end_line; // ')'
    const int base = f.info;
    int nparams = 0;
    if (has_multiple_results(args.k)) {
        nparams = multiple_results;
    } else {
        if (args.k != ExpKind::Void) {
            exp2nextreg(args);
        }
        nparams = fs_->freereg - (base + 1);
    }
    init_exp(f, ExpKind::Call, code_abc(OpCode::Call, base, nparams + 1, 2));
    fixline(line);
    fs_->freereg = base + 1; // the call leaves one result where the function was
}

void Compiler::constructor(const TableExpr& table, ExpDesc& t, int end_line) {
    // constructor: NEWTABLE, then the fields: named ones stored one by one,
    // positional ones gathered in registers and stored by SETLIST in
    // batches of 50.
    const int pc = code_abc(OpCode::NewTable, 0, 0, 0);
    code(0);         // room for the extra argument
    int na = 0;      // positional fields already stored
    int nh = 0;      // named fields
    int tostore = 0; // positional fields waiting in registers
    ExpDesc v;       // the last positional field read
    init_exp(t, ExpKind::NonReloc, fs_->freereg);
    reserveregs(1);
    init_exp(v, ExpKind::Void, 0);
    for (const TableField& field : ast_.fields(table.fields)) {
        // closelistfield
        if (v.k != ExpKind::Void) {
            exp2nextreg(v);
            v.k = ExpKind::Void;
            if (tostore == fields_per_flush) {
                setlist(t.info, na, tostore);
                na += tostore;
                tostore = 0;
            }
        }
        if (field.kind == TableField::Kind::Positional) {
            expr(field.value, v); // listfield
            ++tostore;
        } else { // recfield
            const int reg = fs_->freereg;
            ExpDesc key;
            if (field.kind == TableField::Kind::Keyed) {
                last_line_ = first_line(field.key); // '['
            }
            expr(field.key, key);
            exp2val(key);
            ++nh;
            ExpDesc tab = t;
            indexed(tab, key);
            ExpDesc val;
            expr(field.value, val);
            storevar(tab, val);
            fs_->freereg = reg;
        }
    }
    last_line_ = end_line; // '}'
    // lastlistfield
    if (tostore != 0) {
        if (has_multiple_results(v.k)) {
            setmultret(v);
            setlist(t.info, na, multiple_results);
            --na; // the last item's count is unknown
        } else {
            if (v.k != ExpKind::Void) {
                exp2nextreg(v);
            }
            setlist(t.info, na, tostore);
        }
        na += tostore;
    }
    settablesize(pc, t.info, na, nh);
}

void Compiler::expr(ExprId id, ExpDesc& v) {
    // subexpr and simpleexp, one tree node at a time.
    const int line = ast_.line(id);
    const int end_line = ast_.end_line(id);
    std::visit(Overloaded{
                   [&](const NilExpr&) {
                       init_exp(v, ExpKind::Nil, 0);
                       last_line_ = end_line;
                   },
                   [&](const TrueExpr&) {
                       init_exp(v, ExpKind::True, 0);
                       last_line_ = end_line;
                   },
                   [&](const FalseExpr&) {
                       init_exp(v, ExpKind::False, 0);
                       last_line_ = end_line;
                   },
                   [&](const IntegerExpr& e) {
                       init_exp(v, ExpKind::KInt, 0);
                       v.ival = e.value;
                       last_line_ = end_line;
                   },
                   [&](const FloatExpr& e) {
                       init_exp(v, ExpKind::KFlt, 0);
                       v.nval = e.value;
                       last_line_ = end_line;
                   },
                   [&](const StringExpr& e) {
                       codestring(v, string_of(e.text));
                       last_line_ = end_line;
                   },
                   [&](const VarargExpr&) {
                       init_exp(v, ExpKind::Vararg, code_abc(OpCode::VarArg, 0, 0, 1));
                       last_line_ = end_line;
                   },
                   [&](const UnaryExpr& e) {
                       last_line_ = line; // the operator
                       expr(e.operand, v);
                       prefix(unary_operator(e.op), v, line);
                   },
                   [&](const BinaryExpr& e) {
                       const BinOpr op = binary_operator(e.op);
                       expr(e.left, v);
                       last_line_ = line; // the operator
                       infix(op, v);
                       ExpDesc v2;
                       expr(e.right, v2);
                       posfix(op, v, v2, line);
                   },
                   [&](const NameExpr& e) {
                       last_line_ = end_line;
                       singlevar(string_of(e.name), v);
                   },
                   [&](const ParenExpr& e) {
                       last_line_ = line; // '('
                       expr(e.inner, v);
                       last_line_ = end_line; // ')'
                       dischargevars(v);
                   },
                   [&](const IndexExpr& e) {
                       expr(e.object, v);
                       exp2anyregup(v);
                       ExpDesc key;
                       expr(e.key, key);
                       exp2val(key);
                       last_line_ = end_line; // the name, or ']'
                       indexed(v, key);
                   },
                   [&](const CallExpr& e) {
                       expr(e.function, v);
                       exp2nextreg(v);
                       funcargs(v, e.arguments, line, end_line);
                   },
                   [&](const MethodCallExpr& e) {
                       expr(e.object, v);
                       ExpDesc key;
                       codestring(key, string_of(e.method));
                       last_line_ = e.method_line;
                       self(v, key);
                       funcargs(v, e.arguments, line, end_line);
                   },
                   [&](const TableExpr& e) { constructor(e, v, end_line); },
                   [&](const FunctionExpr& e) {
                       last_line_ = line;
                       body(v, e.function, ast_.function(e.function).line);
                   },
               },
               ast_.expression(id));
}

// ---- lparser.c: statements ----

void Compiler::statlist(std::span<const StatId> statements, bool repeat_body) {
    for (std::size_t i = 0; i < statements.size(); ++i) {
        const Stat& stat = ast_.statement(statements[i]);
        if (const auto* label = std::get_if<LabelStat>(&stat)) {
            // labelstat skips the labels right after it (they are created
            // first, innermost first); a run of labels at the end of a block
            // (not a repeat body, whose `until` sees its locals) is outside
            // the block's locals.
            std::size_t end = i + 1;
            while (end < statements.size() &&
                   std::holds_alternative<LabelStat>(ast_.statement(statements[end]))) {
                ++end;
            }
            const bool last = end == statements.size() && !repeat_body;
            for (std::size_t j = end; j-- > i;) {
                const StatId id = statements[j];
                last_line_ = ast_.end_line(id);
                labelstat(string_of(std::get<LabelStat>(ast_.statement(id)).name), ast_.line(id),
                          last);
                fs_->freereg = nvarstack();
            }
            (void)label;
            i = end - 1;
            continue;
        }
        statement(statements[i]);
    }
}

void Compiler::block(const Block& b) {
    BlockCnt bl;
    enterblock(bl, false);
    statlist(ast_.statements(b));
    leaveblock();
}

void Compiler::statement(StatId id) {
    const int line = ast_.line(id);
    std::visit(Overloaded{
                   [&](const ReturnStat& s) {
                       last_line_ = line;
                       retstat(s);
                       last_line_ = ast_.end_line(id);
                   },
                   [&](const CallStat& s) {
                       ExpDesc v;
                       expr(s.call, v);
                       Instruction& inst = instruction(v);
                       inst = set_c(inst, 1); // a call statement keeps no results
                   },
                   [&](const AssignStat& s) {
                       const auto targets = ast_.expressions(s.targets);
                       LhsAssign v;
                       expr(targets[0], v.v);
                       restassign(v, targets.subspan(1), s.values, 1);
                   },
                   [&](const LocalStat& s) {
                       last_line_ = line;
                       localstat(s, id);
                   },
                   [&](const DoStat& s) {
                       last_line_ = line;
                       block(s.body);
                       last_line_ = ast_.end_line(id);
                   },
                   [&](const WhileStat& s) { whilestat(s, id); },
                   [&](const RepeatStat& s) { repeatstat(s, id); },
                   [&](const IfStat& s) { ifstat(s, id); },
                   [&](const NumericForStat& s) {
                       BlockCnt bl;
                       enterblock(bl, true); // the loop: break jumps to its end
                       last_line_ = line;
                       fornum(s, line);
                       last_line_ = ast_.end_line(id);
                       leaveblock();
                   },
                   [&](const GenericForStat& s) {
                       BlockCnt bl;
                       enterblock(bl, true);
                       last_line_ = line;
                       forlist(s);
                       last_line_ = ast_.end_line(id);
                       leaveblock();
                   },
                   [&](const FunctionStat& s) {
                       last_line_ = line;
                       funcstat(s, line);
                   },
                   [&](const LocalFunctionStat& s) {
                       last_line_ = line;
                       localfunc(s, line);
                   },
                   [&](const GotoStat& s) {
                       last_line_ = ast_.end_line(id);
                       gotostat(s, id);
                   },
                   [&](const LabelStat& s) {
                       last_line_ = ast_.end_line(id);
                       labelstat(string_of(s.name), line, false);
                   },
                   [&](const BreakStat&) {
                       last_line_ = line;
                       newlabelentry(gotos_, break_name_, line, jump());
                   },
               },
               ast_.statement(id));
    fs_->freereg = nvarstack(); // free the registers of temporaries
}

void Compiler::check_conflict(LhsAssign* lh, const ExpDesc& v) {
    // check_conflict: in `t[i], i = ...` or `t.x, t = ...`, an earlier
    // target uses a local or upvalue that a later target changes; the
    // earlier one must use a copy taken before the assignments.
    const int extra = fs_->freereg;
    bool conflict = false;
    for (; lh != nullptr; lh = lh->prev) {
        if (!is_indexed(lh->v.k)) {
            continue;
        }
        if (lh->v.k == ExpKind::IndexUp) {
            if (v.k == ExpKind::Upval && lh->v.ind_t == v.info) {
                conflict = true;
                lh->v.k = ExpKind::IndexStr;
                lh->v.ind_t = extra;
            }
        } else {
            if (v.k == ExpKind::Local && lh->v.ind_t == v.var_ridx) {
                conflict = true;
                lh->v.ind_t = extra;
            }
            if (lh->v.k == ExpKind::Indexed && v.k == ExpKind::Local &&
                lh->v.ind_idx == v.var_ridx) {
                conflict = true;
                lh->v.ind_idx = extra;
            }
        }
    }
    if (conflict) {
        if (v.k == ExpKind::Local) {
            code_abc(OpCode::Move, extra, v.var_ridx, 0);
        } else {
            code_abc(OpCode::GetUpval, extra, v.info, 0);
        }
        reserveregs(1);
    }
}

void Compiler::restassign(LhsAssign& lh, std::span<const ExprId> targets, IdRange<ExprId> values,
                          int nvars) {
    // restassign: compile every target, then the values; the stores happen
    // last to first as the recursion returns (E3.4).
    ExpDesc e;
    if (!targets.empty()) {
        LhsAssign nv;
        nv.prev = &lh;
        expr(targets[0], nv.v);
        if (!is_indexed(nv.v.k)) {
            check_conflict(&lh, nv.v);
        }
        restassign(nv, targets.subspan(1), values, nvars + 1);
    } else {
        const int nexps = explist(values, e);
        if (nexps != nvars) {
            adjust_assign(nvars, nexps, e);
        } else {
            setoneret(e);
            storevar(lh.v, e);
            return;
        }
    }
    init_exp(e, ExpKind::NonReloc, fs_->freereg - 1); // the default: the value on top
    storevar(lh.v, e);
}

int Compiler::cond(ExprId condition) {
    ExpDesc v;
    expr(condition, v);
    if (v.k == ExpKind::Nil) {
        v.k = ExpKind::False; // all falses are equal here
    }
    goiftrue(v);
    return v.f;
}

void Compiler::labelstat(const String* name, int line, bool last) {
    createlabel(name, line, last);
}

void Compiler::gotostat(const GotoStat& s, StatId id) {
    const String* name = string_of(s.label);
    const int line = ast_.line(id);
    const int label = findlabel(name);
    if (label < 0) {
        newlabelentry(gotos_, name, line, jump()); // forward: resolved by the label
        return;
    }
    // Backward: close the upvalues of the variables it leaves.
    const int lblevel = reglevel(fs_, labels_[index(label)].nactvar);
    if (nvarstack() > lblevel) {
        code_abc(OpCode::Close, lblevel, 0, 0);
    }
    patchlist(jump(), labels_[index(label)].pc);
}

void Compiler::whilestat(const WhileStat& s, StatId id) {
    last_line_ = ast_.line(id);
    const int whileinit = getlabel();
    const int condexit = cond(s.condition);
    BlockCnt bl;
    enterblock(bl, true);
    block(s.body);
    patchlist(jump(), whileinit);
    last_line_ = ast_.end_line(id); // 'end'
    leaveblock();
    patchtohere(condexit);
}

void Compiler::repeatstat(const RepeatStat& s, StatId id) {
    const int repeat_init = getlabel();
    BlockCnt bl1;
    BlockCnt bl2;
    enterblock(bl1, true);  // the loop
    enterblock(bl2, false); // the scope, through the condition
    last_line_ = ast_.line(id);
    statlist(ast_.statements(s.body), true);
    int condexit = cond(s.condition);
    leaveblock(); // bl2
    if (bl2.upval) {
        // Repeating must close the body's upvalues first.
        const int exit = jump();
        patchtohere(condexit);
        code_abc(OpCode::Close, reglevel(fs_, bl2.nactvar), 0, 0);
        condexit = jump();
        patchtohere(exit);
    }
    patchlist(condexit, repeat_init);
    leaveblock(); // bl1
}

void Compiler::exp1(ExprId e) {
    ExpDesc v;
    expr(e, v);
    exp2nextreg(v);
}

void Compiler::fixforjump(int pc, int dest, bool back) {
    int offset = dest - (pc + 1);
    if (back) {
        offset = -offset;
    }
    if (offset > max_bx) {
        error("control structure too long");
        return;
    }
    Instruction& jmp = fs_->f->code[index(pc)];
    jmp = set_bx(jmp, offset);
}

void Compiler::forbody(int base, int line, int nvars, bool isgen, const Block& b) {
    // forbody: the loop variables are in a block around the body.
    const int prep = code_abx(isgen ? OpCode::TForPrep : OpCode::ForPrep, base, 0);
    BlockCnt bl;
    enterblock(bl, false);
    adjustlocalvars(nvars);
    reserveregs(nvars);
    block(b);
    leaveblock();
    fixforjump(prep, getlabel(), false);
    if (isgen) {
        code_abc(OpCode::TForCall, base, 0, nvars);
        fixline(line);
    }
    const int endfor = code_abx(isgen ? OpCode::TForLoop : OpCode::ForLoop, base, 0);
    fixforjump(endfor, prep + 1, true);
    fixline(line);
}

void Compiler::fornum(const NumericForStat& s, int line) {
    // fornum: three hidden variables (start, limit, step), then the loop
    // variable.
    const int base = fs_->freereg;
    new_localvar(for_state_name_);
    new_localvar(for_state_name_);
    new_localvar(for_state_name_);
    new_localvar(string_of(s.variable));
    exp1(s.start);
    exp1(s.limit);
    if (s.has_step) {
        exp1(s.step);
    } else {
        load_int(fs_->freereg, 1);
        reserveregs(1);
    }
    adjustlocalvars(3);
    forbody(base, line, 1, false, s.body);
}

void Compiler::forlist(const GenericForStat& s) {
    // forlist: four hidden variables (iterator, state, control, closing),
    // then the loop variables.
    const int base = fs_->freereg;
    const auto names = ast_.local_names(s.names);
    const int nvars = 4 + static_cast<int>(names.size());
    for (int i = 0; i < 4; ++i) {
        new_localvar(for_state_name_);
    }
    for (const LocalName& name : names) {
        new_localvar(string_of(name.name));
    }
    const int line = first_line(ast_.expressions(s.values)[0]);
    ExpDesc e;
    const int nexps = explist(s.values, e);
    adjust_assign(4, nexps, e);
    adjustlocalvars(4);
    marktobeclosed(fs_); // the closing value is to be closed
    checkstack(3);       // room to call the iterator
    forbody(base, line, nvars - 4, true, s.body);
}

void Compiler::test_then_block(int& escapelist, const IfClause& clause, bool more) {
    // test_then_block: [if | elseif] cond then block
    BlockCnt bl;
    ExpDesc v;
    expr(clause.condition, v);
    last_line_ = clause.then_line; // 'then'
    const auto statements = ast_.statements(clause.body);
    int jf = no_jump;
    if (!statements.empty() && std::holds_alternative<BreakStat>(ast_.statement(statements[0]))) {
        // `if cond then break`: jump straight out of the loop when true.
        const int line = ast_.line(statements[0]);
        goiffalse(v);
        last_line_ = line;
        enterblock(bl, false);
        newlabelentry(gotos_, break_name_, line, v.t);
        if (statements.size() == 1) {
            leaveblock();
            return;
        }
        jf = jump(); // skip the rest when false
        statlist(statements.subspan(1));
    } else {
        goiftrue(v);
        enterblock(bl, false);
        jf = v.f;
        statlist(statements);
    }
    leaveblock();
    if (more) {
        concat(escapelist, jump()); // jump over the other clauses
    }
    patchtohere(jf);
}

void Compiler::ifstat(const IfStat& s, StatId id) {
    last_line_ = ast_.line(id);
    int escapelist = no_jump;
    const auto clauses = ast_.if_clauses(s.clauses);
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        test_then_block(escapelist, clauses[i], i + 1 < clauses.size() || s.has_else);
    }
    if (s.has_else) {
        block(s.else_body);
    }
    last_line_ = ast_.end_line(id); // 'end'
    patchtohere(escapelist);
}

void Compiler::localfunc(const LocalFunctionStat& s, int line) {
    // localfunc: the variable is active in its own body (recursion), but
    // its debug entry starts after the closure is made.
    ExpDesc b;
    const int fvar = fs_->nactvar;
    new_localvar(string_of(s.name));
    adjustlocalvars(1);
    body(b, s.function, line);
    localdebuginfo(fs_, fvar)->start_pc = fs_->pc();
}

void Compiler::checktoclose(int level) {
    if (level != -1) {
        marktobeclosed(fs_);
        code_abc(OpCode::Tbc, reglevel(fs_, level), 0, 0);
    }
}

void Compiler::localstat(const LocalStat& s, StatId id) {
    // localstat: LOCAL NAME ATTRIB { ',' NAME ATTRIB } ['=' explist]
    int toclose = -1;
    int nvars = 0;
    int vidx = 0;
    for (const LocalName& name : ast_.local_names(s.names)) {
        vidx = new_localvar(string_of(name.name));
        VarKind kind = VarKind::Regular;
        if (name.attribute == LocalName::Attribute::Const) {
            kind = VarKind::Const;
        } else if (name.attribute == LocalName::Attribute::Close) {
            kind = VarKind::ToClose;
            toclose = fs_->nactvar + nvars;
        }
        getlocalvardesc(fs_, vidx).kind = kind;
        ++nvars;
    }
    ExpDesc e;
    int nexps = 0;
    if (s.values.count != 0) {
        nexps = explist(s.values, e);
    } else {
        e.k = ExpKind::Void;
        last_line_ = ast_.end_line(id);
    }
    VarDesc& var = getlocalvardesc(fs_, vidx); // the last variable
    Value k;
    if (nvars == nexps && var.kind == VarKind::Const && exp2const(e, k)) {
        // A <const> with a constant value takes no register.
        var.kind = VarKind::CompileTimeConst;
        var.k = k;
        adjustlocalvars(nvars - 1);
        fs_->nactvar++;
    } else {
        adjust_assign(nvars, nexps, e);
        adjustlocalvars(nvars);
    }
    checktoclose(toclose);
}

void Compiler::funcstat(const FunctionStat& s, int line) {
    // funcstat: FUNCTION funcname body
    ExpDesc v;
    ExpDesc b;
    expr(s.target, v);
    body(b, s.function, line);
    storevar(v, b);
    fixline(line); // the definition "happens" on the first line
}

void Compiler::retstat(const ReturnStat& s) {
    int first = nvarstack();
    int nret = 0;
    if (s.values.count != 0) {
        ExpDesc e;
        nret = explist(s.values, e);
        if (has_multiple_results(e.k)) {
            setmultret(e);
            if (e.k == ExpKind::Call && nret == 1 && !fs_->bl->insidetbc) {
                // A tail call (E6.5).
                Instruction& inst = instruction(e);
                inst = (inst & ~Instruction{0x7f}) | static_cast<Instruction>(OpCode::TailCall);
            }
            nret = multiple_results;
        } else if (nret == 1) {
            first = exp2anyreg(e); // the value can stay where it is
        } else {
            exp2nextreg(e);
        }
    }
    ret(first, nret);
}

// ---- The main function ----

std::expected<Proto*, SyntaxError> Compiler::compile_main() {
    // mainfunc: a vararg function with _ENV as its only upvalue.
    FuncState fs;
    BlockCnt bl;
    fs.f = heap_.new_proto();
    open_func(fs, bl);
    setvararg(0);
    fs.f->upvalues.push_back(Proto::UpvalueInfo{.name = env_name_, .in_stack = true, .index = 0});
    const Function& main = ast_.function(ast_.main_function());
    statlist(ast_.statements(main.body));
    last_line_ = main.end_line;
    close_func();
    if (error_) {
        return std::unexpected(std::move(*error_));
    }
    return fs.f;
}

} // namespace

std::expected<Proto*, SyntaxError> compile(Heap& heap, const Ast& ast, std::string_view source,
                                           std::string_view chunk_name) {
    Compiler compiler{heap, ast, source, chunk_name};
    return compiler.compile_main();
}

} // namespace lua2
