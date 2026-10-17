#include "vm/proto.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "value/convert.hpp"

namespace lua2 {

namespace {

// The names of the metamethod events, in Lua's TMS order (ltm.h); MMBIN's C
// operand is one of them.
constexpr std::array<std::string_view, 25> event_names{
    "__index", "__newindex", "__gc",  "__mode", "__len",    "__eq",   "__add",   "__sub", "__mul",
    "__mod",   "__pow",      "__div", "__idiv", "__band",   "__bor",  "__bxor",  "__shl", "__shr",
    "__unm",   "__bnot",     "__lt",  "__le",   "__concat", "__call", "__close",
};

std::string_view event_name(int event) {
    return event >= 0 && static_cast<std::size_t>(event) < event_names.size()
               ? event_names.at(static_cast<std::size_t>(event))
               : "?";
}

// A string the way luac prints it: in double quotes, with C escapes for the
// usual control characters and \ddd for any other unprintable byte.
std::string luac_quoted(std::string_view bytes) {
    std::string out = "\"";
    for (const char ch : bytes) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\a': out += "\\a"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\v': out += "\\v"; break;
        default:
            if (c >= 0x20 && c < 0x7f) {
                out += static_cast<char>(c);
            } else {
                out += std::format("\\{:03d}", c);
            }
        }
    }
    out += '"';
    return out;
}

std::string constant_text(const Proto& proto, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= proto.constants.size()) {
        return "?";
    }
    const Value k = proto.constants.at(static_cast<std::size_t>(index));
    if (k.is_string()) {
        return luac_quoted(k.as_string()->view());
    }
    if (const auto number = number_text(k)) {
        return std::string{number->view()};
    }
    if (k.is_boolean()) {
        return k.as_boolean() ? "true" : "false";
    }
    return "nil";
}

// One letter for the constant's type, as luac -l -l shows them.
char constant_kind(Value k) {
    switch (k.tag()) {
    case Tag::Nil: return 'N';
    case Tag::False:
    case Tag::True: return 'B';
    case Tag::Integer: return 'I';
    case Tag::Float: return 'F';
    case Tag::ShortString:
    case Tag::LongString: return 'S';
    case Tag::CFunction:
    case Tag::LuaClosure:
    case Tag::CClosure:
    case Tag::LightUserdata:
    case Tag::Userdata:
    case Tag::Table: break; // never a constant
    }
    return '?';
}

std::string name_text(const String* name) {
    return name != nullptr ? std::string{name->view()} : std::string{"-"};
}

std::string upvalue_name(const Proto& proto, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= proto.upvalues.size()) {
        return "-";
    }
    return name_text(proto.upvalues.at(static_cast<std::size_t>(index)).name);
}

// The Ax of the EXTRAARG after instruction `pc` (NEWTABLE and SETLIST use it
// for numbers that do not fit in C), or 0.
int extra_arg(const Proto& proto, int pc) {
    const auto next = static_cast<std::size_t>(pc) + 1;
    return next < proto.code.size() && get_op(proto.code.at(next)) == OpCode::ExtraArg
               ? get_ax(proto.code.at(next))
               : 0;
}

// "RK" operands: a constant when k is set, a register otherwise.
std::string rk(int c, bool k) {
    return k ? std::format("{}k", c) : std::format("{}", c);
}

// The operands of instruction `pc`, and its comment (after "; ").
std::pair<std::string, std::string> operands(const Proto& proto, int pc) {
    const Instruction i = proto.code.at(static_cast<std::size_t>(pc));
    const int a = get_a(i);
    const int b = get_b(i);
    const int c = get_c(i);
    const bool k = get_k(i);
    const int bx = get_bx(i);
    const auto out = [](int n) { return n == 0 ? std::string{"all"} : std::format("{}", n - 1); };
    const auto in = [](int n) { return n == 0 ? std::string{"all"} : std::format("{}", n - 1); };
    switch (get_op(i)) {
        using enum OpCode;
    case Move: return {std::format("{} {}", a, b), ""};
    case LoadI:
    case LoadF: return {std::format("{} {}", a, get_sbx(i)), ""};
    case LoadK: return {std::format("{} {}", a, bx), constant_text(proto, bx)};
    case LoadKX: {
        const auto next = static_cast<std::size_t>(pc) + 1;
        const int index = next < proto.code.size() ? get_ax(proto.code.at(next)) : -1;
        return {std::format("{}", a), constant_text(proto, index)};
    }
    case LoadFalse:
    case LFalseSkip:
    case LoadTrue:
    case Close:
    case Tbc:
    case Return1:
    case VarArgPrep: return {std::format("{}", a), ""};
    case LoadNil: return {std::format("{} {}", a, b), std::format("{} out", b + 1)};
    case GetUpval:
    case SetUpval: return {std::format("{} {}", a, b), upvalue_name(proto, b)};
    case GetTabUp:
        return {std::format("{} {} {}", a, b, c),
                upvalue_name(proto, b) + " " + constant_text(proto, c)};
    case GetTable:
    case GetI:
    case Add:
    case Sub:
    case Mul:
    case Mod:
    case Pow:
    case Div:
    case IDiv:
    case BAnd:
    case BOr:
    case BXor:
    case Shl:
    case Shr: return {std::format("{} {} {}", a, b, c), ""};
    case GetField: return {std::format("{} {} {}", a, b, c), constant_text(proto, c)};
    case SetTabUp:
        return {std::format("{} {} {}", a, b, rk(c, k)),
                upvalue_name(proto, a) + " " + constant_text(proto, b) +
                    (k ? " " + constant_text(proto, c) : "")};
    case SetTable:
    case SetI: return {std::format("{} {} {}", a, b, rk(c, k)), k ? constant_text(proto, c) : ""};
    case SetField:
        return {std::format("{} {} {}", a, b, rk(c, k)),
                constant_text(proto, b) + (k ? " " + constant_text(proto, c) : "")};
    case NewTable:
        return {std::format("{} {} {}", a, b, c),
                std::format("{}", c + (extra_arg(proto, pc) * (max_c + 1)))};
    case Self: return {std::format("{} {} {}", a, b, rk(c, k)), k ? constant_text(proto, c) : ""};
    case AddI:
    case ShrI:
    case ShlI: return {std::format("{} {} {}", a, b, get_sc(i)), ""};
    case AddK:
    case SubK:
    case MulK:
    case ModK:
    case PowK:
    case DivK:
    case IDivK:
    case BAndK:
    case BOrK:
    case BXorK: return {std::format("{} {} {}", a, b, c), constant_text(proto, c)};
    case MmBin: return {std::format("{} {} {}", a, b, c), std::string{event_name(c)}};
    case MmBinI:
        return {std::format("{} {} {} {}", a, get_sb(i), c, k ? 1 : 0),
                std::string{event_name(c)} + (k ? " flip" : "")};
    case MmBinK:
        return {std::format("{} {} {} {}", a, b, c, k ? 1 : 0),
                std::string{event_name(c)} + " " + constant_text(proto, b) + (k ? " flip" : "")};
    case Unm:
    case BNot:
    case Not:
    case Len:
    case Concat: return {std::format("{} {}", a, b), ""};
    case Jmp: return {std::format("{}", get_sj(i)), std::format("to {}", get_sj(i) + pc + 2)};
    case Eq:
    case Lt:
    case Le: return {std::format("{} {} {}", a, b, k ? 1 : 0), ""};
    case EqK: return {std::format("{} {} {}", a, b, k ? 1 : 0), constant_text(proto, b)};
    case EqI:
    case LtI:
    case LeI:
    case GtI:
    case GeI: return {std::format("{} {} {}", a, get_sb(i), k ? 1 : 0), ""};
    case Test: return {std::format("{} {}", a, k ? 1 : 0), ""};
    case TestSet: return {std::format("{} {} {}", a, b, k ? 1 : 0), ""};
    case Call:
        return {std::format("{} {} {}", a, b, c), std::format("{} in {} out", in(b), out(c))};
    case TailCall:
        return {std::format("{} {} {}{}", a, b, c, k ? "k" : ""), std::format("{} in", b - 1)};
    case Return:
        return {std::format("{} {} {}{}", a, b, c, k ? "k" : ""), std::format("{} out", out(b))};
    case Return0: return {"", ""};
    case ForLoop:
    case TForLoop: return {std::format("{} {}", a, bx), std::format("to {}", pc - bx + 2)};
    case ForPrep: return {std::format("{} {}", a, bx), std::format("exit to {}", pc + bx + 3)};
    case TForPrep: return {std::format("{} {}", a, bx), std::format("to {}", pc + bx + 2)};
    case TForCall: return {std::format("{} {}", a, c), ""};
    case SetList:
        return {std::format("{} {} {}", a, b, c),
                k ? std::format("{}", c + (extra_arg(proto, pc) * (max_c + 1))) : ""};
    case Closure: return {std::format("{} {}", a, bx), ""};
    case VarArg: return {std::format("{} {}", a, c), std::format("{} out", out(c))};
    case ExtraArg: return {std::format("{}", get_ax(i)), ""};
    }
    return {"?", ""};
}

std::string plural(std::size_t n, std::string_view word) {
    return std::format("{} {}{}", n, word, n == 1 ? "" : "s");
}

// The chunk name as luac shows it: a file name without its '@', a '='
// name without the '=', and "(string)" for source text used as a name.
std::string source_text(const Proto& proto) {
    if (proto.source == nullptr) {
        return "?";
    }
    const std::string_view s = proto.source->view();
    if (s.starts_with('@') || s.starts_with('=')) {
        return std::string{s.substr(1)};
    }
    return "(string)";
}

void list_into(std::string& out, const Proto& proto) {
    // Header.
    out += std::format("\n{} <{}:{},{}> ({})\n", proto.line_defined == 0 ? "main" : "function",
                       source_text(proto), proto.line_defined, proto.last_line_defined,
                       plural(proto.code.size(), "instruction"));
    out += std::format(
        "{}{} param{}, {}, {}, {}, {}, {}\n", proto.parameter_count, proto.is_vararg ? "+" : "",
        proto.parameter_count == 1 ? "" : "s", plural(proto.max_stack_size, "slot"),
        plural(proto.upvalues.size(), "upvalue"), plural(proto.locals.size(), "local"),
        plural(proto.constants.size(), "constant"), plural(proto.protos.size(), "function"));

    // Code.
    for (std::size_t pc = 0; pc < proto.code.size(); ++pc) {
        const auto op = get_op(proto.code.at(pc));
        const std::string line =
            pc < proto.lines.size() ? std::format("[{}]", proto.lines.at(pc)) : std::string{"[-]"};
        const auto [args, comment] = operands(proto, static_cast<int>(pc));
        out += std::format("\t{}\t{}\t{:<9}\t{}", pc + 1, line, op_info(op).name, args);
        if (!comment.empty()) {
            out += "\t; " + comment;
        }
        out += '\n';
    }

    // Constants, locals and upvalues.
    out += std::format("constants ({}):\n", proto.constants.size());
    for (std::size_t n = 0; n < proto.constants.size(); ++n) {
        out += std::format("\t{}\t{}\t{}\n", n, constant_kind(proto.constants.at(n)),
                           constant_text(proto, static_cast<int>(n)));
    }
    out += std::format("locals ({}):\n", proto.locals.size());
    for (std::size_t n = 0; n < proto.locals.size(); ++n) {
        const auto& local = proto.locals.at(n);
        out += std::format("\t{}\t{}\t{}\t{}\n", n, name_text(local.name), local.start_pc + 1,
                           local.end_pc + 1);
    }
    out += std::format("upvalues ({}):\n", proto.upvalues.size());
    for (std::size_t n = 0; n < proto.upvalues.size(); ++n) {
        const auto& upvalue = proto.upvalues.at(n);
        out += std::format("\t{}\t{}\t{}\t{}\n", n, name_text(upvalue.name),
                           upvalue.in_stack ? 1 : 0, upvalue.index);
    }

    for (const auto& nested : proto.protos) {
        list_into(out, *nested);
    }
}

} // namespace

std::string list_proto(const Proto& proto) {
    std::string out;
    list_into(out, proto);
    return out;
}

std::string chunk_id(std::string_view source) {
    // Lua's luaO_chunkid, with its LUA_IDSIZE of 60 bytes (the '\0' included).
    constexpr std::size_t id_size = 60;
    constexpr std::string_view dots = "...";
    if (source.starts_with('=')) { // a name to show as it is, cut to fit
        return std::string{source.substr(1, id_size - 1)};
    }
    if (source.starts_with('@')) { // a file name: its end is the useful part
        if (source.size() <= id_size) {
            return std::string{source.substr(1)};
        }
        return std::string{dots} + std::string{source.substr(source.size() - (id_size - 4))};
    }
    // Source text: its first line, cut to fit, as [string "..."].
    constexpr std::size_t room = id_size - std::string_view{"[string \"...\"]"}.size() - 1;
    const std::size_t newline = source.find('\n');
    std::string out = "[string \"";
    if (source.size() < room && newline == std::string_view::npos) {
        out += source;
    } else {
        const std::string_view first = source.substr(0, newline);
        out += first.substr(0, room);
        out += dots;
    }
    out += "\"]";
    return out;
}

} // namespace lua2
