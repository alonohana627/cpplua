// What error messages say about values: a port of the "symbolic execution"
// part of Lua's ldebug.c (findsetreg, getobjname, funcnamefromcode).

#include "vm/debug_info.hpp"

#include <cstddef>
#include <format>
#include <string_view>

#include "vm/opcodes.hpp"

namespace lua2 {

namespace {

constexpr std::size_t index(int i) {
    return static_cast<std::size_t>(i);
}

Instruction code_at(const Proto& p, int pc) {
    return p.code[index(pc)];
}

std::string_view upvalue_name(const Proto& p, int uv) {
    // upvalname
    const String* name = p.upvalues[index(uv)].name;
    return name == nullptr ? std::string_view{"?"} : name->view();
}

// filterpc: code after a jump into it may not have run.
int filter_pc(int pc, int jump_target) {
    return pc < jump_target ? -1 : pc;
}

// findsetreg: the last instruction before `last_pc` that changed `reg`.
int find_set_register(const Proto& p, int last_pc, int reg) {
    if (op_info(get_op(code_at(p, last_pc))).metamethod) {
        --last_pc; // an MMBIN: the operation before it did not finish
    }
    int set_reg = -1;
    int jump_target = 0; // code before this address is conditional
    for (int pc = 0; pc < last_pc; ++pc) {
        const Instruction i = code_at(p, pc);
        const OpCode op = get_op(i);
        const int a = get_a(i);
        bool change = false;
        switch (op) {
        case OpCode::LoadNil: change = a <= reg && reg <= a + get_b(i); break;
        case OpCode::TForCall: change = reg >= a + 2; break;
        case OpCode::Call:
        case OpCode::TailCall: change = reg >= a; break;
        case OpCode::Jmp: {
            const int dest = pc + 1 + get_sj(i);
            if (dest <= last_pc && dest > jump_target) {
                jump_target = dest;
            }
            break;
        }
        default: change = op_info(op).sets_a && reg == a; break;
        }
        if (change) {
            set_reg = filter_pc(pc, jump_target);
        }
    }
    return set_reg;
}

struct ObjectName {
    std::string_view kind; // empty: no name
    std::string_view name;
};

// kname
ObjectName constant_name(const Proto& p, int index_k) {
    const Value k = p.constants[index(index_k)];
    if (k.is_string()) {
        return {.kind = "constant", .name = k.as_string()->view()};
    }
    return {.kind = {}, .name = "?"};
}

// basicgetobjname: a local, an upvalue, or a constant.
ObjectName basic_object_name(const Proto& p, int& pc, int reg) {
    if (const String* name = local_name(p, reg + 1, pc)) {
        return {.kind = "local", .name = name->view()};
    }
    pc = find_set_register(p, pc, reg);
    if (pc != -1) {
        const Instruction i = code_at(p, pc);
        switch (get_op(i)) {
        case OpCode::Move: {
            const int b = get_b(i);
            if (b < get_a(i)) {
                return basic_object_name(p, pc, b);
            }
            break;
        }
        case OpCode::GetUpval: return {.kind = "upvalue", .name = upvalue_name(p, get_b(i))};
        case OpCode::LoadK: return constant_name(p, get_bx(i));
        case OpCode::LoadKX: return constant_name(p, get_ax(code_at(p, pc + 1)));
        default: break;
        }
    }
    return {};
}

// rname: a register's name, if it is a constant's.
std::string_view register_constant_name(const Proto& p, int pc, int c) {
    const ObjectName found = basic_object_name(p, pc, c);
    return found.kind == "constant" ? found.name : std::string_view{"?"};
}

// rkname
std::string_view rk_name(const Proto& p, int pc, Instruction i) {
    const int c = get_c(i);
    if (get_k(i)) {
        return constant_name(p, c).name;
    }
    return register_constant_name(p, pc, c);
}

// isEnv: "global" when the indexed table is _ENV, "field" otherwise.
std::string_view env_kind(const Proto& p, int pc, Instruction i, bool is_upvalue) {
    const int t = get_b(i);
    std::string_view name;
    if (is_upvalue) {
        name = upvalue_name(p, t);
    } else {
        const ObjectName found = basic_object_name(p, pc, t);
        if (found.kind == "local" || found.kind == "upvalue") {
            name = found.name;
        }
    }
    return name == "_ENV" ? "global" : "field";
}

// getobjname: basicgetobjname plus table accesses.
ObjectName object_name(const Proto& p, int last_pc, int reg) {
    const ObjectName basic = basic_object_name(p, last_pc, reg);
    if (!basic.kind.empty()) {
        return basic;
    }
    if (last_pc == -1) {
        return {};
    }
    const Instruction i = code_at(p, last_pc);
    switch (get_op(i)) {
    case OpCode::GetTabUp:
        return {.kind = env_kind(p, last_pc, i, true), .name = constant_name(p, get_c(i)).name};
    case OpCode::GetTable:
        return {
            .kind = env_kind(p, last_pc, i, false),
            .name = register_constant_name(p, last_pc, get_c(i)),
        };
    case OpCode::GetI: return {.kind = "field", .name = "integer index"};
    case OpCode::GetField:
        return {.kind = env_kind(p, last_pc, i, false), .name = constant_name(p, get_c(i)).name};
    case OpCode::Self: return {.kind = "method", .name = rk_name(p, last_pc, i)};
    default: break;
    }
    return {};
}

std::string format_info(const ObjectName& found) {
    // formatvarinfo
    if (found.kind.empty()) {
        return {};
    }
    return std::format(" ({} '{}')", found.kind, found.name);
}

} // namespace

int line_of(const Proto& proto, int pc) {
    if (pc < 0 || index(pc) >= proto.lines.size()) {
        return -1;
    }
    return proto.lines[index(pc)];
}

const String* local_name(const Proto& proto, int n, int pc) {
    // luaF_getlocalname
    for (const auto& local : proto.locals) {
        if (local.start_pc > pc) {
            break;
        }
        if (pc < local.end_pc && --n == 0) {
            return local.name;
        }
    }
    return nullptr;
}

std::string register_info(const Proto& proto, int pc, int reg) {
    return format_info(object_name(proto, pc, reg));
}

std::string upvalue_info(const Proto& proto, int index_up) {
    return std::format(" (upvalue '{}')", upvalue_name(proto, index_up));
}

std::string called_info(const Proto& proto, int pc) {
    const Instruction i = code_at(proto, pc);
    switch (get_op(i)) {
    case OpCode::Call:
    case OpCode::TailCall: return format_info(object_name(proto, pc, get_a(i)));
    case OpCode::TForCall: return " (for iterator 'for iterator')";
    default: return {};
    }
}

} // namespace lua2
