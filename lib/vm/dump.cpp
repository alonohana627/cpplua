// Binary chunks: a port of Lua 5.4.9's ldump.c and lundump.c
// (docs/final_docs/dump.md). cpplua's bytecode is Lua's, so its binary
// chunks are Lua 5.4's: string.dump's output loads in the real Lua, and
// luac's output loads in cpplua.
//
// The one difference inside: cpplua keeps every instruction's absolute line,
// where Lua keeps a byte per instruction (the change from the line before)
// and an absolute line every 128 instructions or after a big jump
// (lcode.c's savelineinfo). Dumping makes Lua's encoding from the lines;
// loading turns it back into lines.

#include "vm/dump.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/heap.hpp"
#include "value/string.hpp"
#include "vm/function.hpp"
#include "vm/proto.hpp"

namespace cpplua {

namespace {

// lundump.h and lcode.h
constexpr std::string_view signature = "\x1bLua";
constexpr std::uint8_t version = 0x54;
constexpr std::uint8_t format = 0;
constexpr std::string_view check_data = "\x19\x93\r\n\x1a\n";
constexpr std::int64_t check_integer = 0x5678;
constexpr double check_number = 370.5;
constexpr int abs_line_info = -0x80;        // ABSLINEINFO
constexpr int line_difference_limit = 0x80; // LIMLINEDIFF
constexpr int max_without_absolute = 128;   // MAXIWTHABS

// The tags of constants in a binary chunk (LUA_V*).
constexpr std::uint8_t tag_nil = 0;
constexpr std::uint8_t tag_false = 1;
constexpr std::uint8_t tag_true = 17;
constexpr std::uint8_t tag_integer = 3;
constexpr std::uint8_t tag_float = 19;
constexpr std::uint8_t tag_short_string = 4;
constexpr std::uint8_t tag_long_string = 20;

// ---- Dumping (ldump.c) ----

class Dumper {
public:
    explicit Dumper(bool strip) : strip_(strip) {}

    std::string take() { return std::move(out_); }

    void header() {
        out_ += signature;
        byte(version);
        byte(format);
        out_ += check_data;
        byte(sizeof(Instruction));
        byte(sizeof(std::int64_t));
        byte(sizeof(double));
        raw(check_integer);
        raw(check_number);
    }

    void byte(std::uint8_t b) { out_ += static_cast<char>(b); }

    void function(const Proto& f, const String* parent_source) {
        string(strip_ || f.source == parent_source ? nullptr : f.source);
        size(static_cast<std::size_t>(f.line_defined));
        size(static_cast<std::size_t>(f.last_line_defined));
        byte(f.parameter_count);
        byte(f.is_vararg ? 1 : 0);
        byte(f.max_stack_size);
        size(f.code.size());
        for (const Instruction i : f.code) {
            raw(i);
        }
        constants(f);
        size(f.upvalues.size());
        for (const Proto::UpvalueInfo& u : f.upvalues) {
            byte(u.in_stack ? 1 : 0);
            byte(u.index);
            byte(u.kind);
        }
        size(f.protos.size());
        for (const Proto* p : f.protos) {
            function(*p, f.source);
        }
        debug(f);
    }

private:
    template <class T>
    void raw(T value) {
        std::array<char, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(T));
        out_.append(bytes.data(), sizeof(T));
    }

    // dumpSize: 7 bits a byte, most significant first, the last byte marked.
    void size(std::size_t x) {
        std::array<std::uint8_t, ((sizeof(std::size_t) * CHAR_BIT) + 6) / 7> buffer{};
        std::size_t n = 0;
        do {
            buffer.at(buffer.size() - ++n) = static_cast<std::uint8_t>(x & 0x7fU);
            x >>= 7U;
        } while (x != 0);
        buffer.back() |= 0x80U;
        for (std::size_t j = buffer.size() - n; j < buffer.size(); ++j) {
            byte(buffer.at(j));
        }
    }

    void string(const String* s) {
        if (s == nullptr) {
            size(0);
            return;
        }
        size(s->length() + 1);
        out_ += s->view();
    }

    void constants(const Proto& f) {
        size(f.constants.size());
        for (const Value k : f.constants) {
            switch (k.tag()) {
            case Tag::Nil: byte(tag_nil); break;
            case Tag::False: byte(tag_false); break;
            case Tag::True: byte(tag_true); break;
            case Tag::Integer:
                byte(tag_integer);
                raw(k.as_integer());
                break;
            case Tag::Float:
                byte(tag_float);
                raw(k.as_float());
                break;
            case Tag::ShortString:
                byte(tag_short_string);
                string(k.as_string());
                break;
            default: // only long strings are left among constants
                byte(tag_long_string);
                string(k.as_string());
                break;
            }
        }
    }

    // dumpDebug, with Lua's line encoding made from the absolute lines.
    void debug(const Proto& f) {
        if (strip_ || f.lines.empty()) {
            size(0); // lineinfo
            size(0); // abslineinfo
        } else {
            std::vector<std::int8_t> relative;
            std::vector<std::pair<int, int>> absolute; // pc, line
            int previous = f.line_defined;
            int without_absolute = 0;
            for (std::size_t pc = 0; pc < f.lines.size(); ++pc) {
                const int line = f.lines[pc];
                int difference = line - previous;
                if (difference >= line_difference_limit || difference <= -line_difference_limit ||
                    without_absolute++ >= max_without_absolute) {
                    absolute.emplace_back(static_cast<int>(pc), line);
                    difference = abs_line_info;
                    without_absolute = 1;
                }
                relative.push_back(static_cast<std::int8_t>(difference));
                previous = line;
            }
            size(relative.size());
            for (const std::int8_t d : relative) {
                byte(static_cast<std::uint8_t>(d));
            }
            size(absolute.size());
            for (const auto& [pc, line] : absolute) {
                size(static_cast<std::size_t>(pc));
                size(static_cast<std::size_t>(line));
            }
        }
        if (strip_) {
            size(0);
            size(0);
            return;
        }
        size(f.locals.size());
        for (const Proto::LocalInfo& local : f.locals) {
            string(local.name);
            size(static_cast<std::size_t>(local.start_pc));
            size(static_cast<std::size_t>(local.end_pc));
        }
        size(f.upvalues.size());
        for (const Proto::UpvalueInfo& u : f.upvalues) {
            string(u.name);
        }
    }

    bool strip_;
    std::string out_;
};

// ---- Loading (lundump.c) ----

class Loader {
public:
    Loader(Heap& heap, std::string_view bytes, std::string name)
        : heap_(heap), bytes_(bytes), name_(std::move(name)) {}

    // luaU_undump: the main function's prototype and its upvalue count.
    bool load(Proto*& main, int& upvalue_count) {
        if (!header()) {
            return false;
        }
        std::uint8_t n = 0;
        if (!byte(n)) {
            return false;
        }
        upvalue_count = n;
        main = heap_.new_proto();
        return function(*main, nullptr);
    }

    [[nodiscard]] const std::string& error() const { return error_; }

private:
    bool fail(std::string_view why) {
        if (error_.empty()) {
            error_ = std::format("{}: bad binary format ({})", name_, why);
        }
        return false;
    }

    bool block(void* out, std::size_t n) {
        if (bytes_.size() - at_ < n) {
            return fail("truncated chunk");
        }
        std::memcpy(out, bytes_.data() + at_, n);
        at_ += n;
        return true;
    }

    bool byte(std::uint8_t& out) { return block(&out, 1); }

    template <class T>
    bool raw(T& out) {
        return block(&out, sizeof(T));
    }

    // loadUnsigned
    bool unsigned_value(std::size_t limit, std::size_t& out) {
        std::size_t x = 0;
        limit >>= 7U;
        std::uint8_t b = 0;
        do {
            if (!byte(b)) {
                return false;
            }
            if (x >= limit) {
                return fail("integer overflow");
            }
            x = (x << 7U) | (b & 0x7fU);
        } while ((b & 0x80U) == 0);
        out = x;
        return true;
    }

    bool size(std::size_t& out) {
        return unsigned_value(std::numeric_limits<std::size_t>::max(), out);
    }

    bool integer(int& out) {
        std::size_t x = 0;
        if (!unsigned_value(static_cast<std::size_t>(std::numeric_limits<int>::max()), x)) {
            return false;
        }
        out = static_cast<int>(x);
        return true;
    }

    // loadStringN: nullptr for "no string".
    bool string(const String*& out) {
        std::size_t n = 0;
        if (!size(n)) {
            return false;
        }
        if (n == 0) {
            out = nullptr;
            return true;
        }
        --n;
        if (bytes_.size() - at_ < n) {
            return fail("truncated chunk");
        }
        out = heap_.new_string(bytes_.substr(at_, n));
        at_ += n;
        return true;
    }

    bool literal(std::string_view expected, std::string_view message) {
        if (bytes_.size() - at_ < expected.size()) {
            return fail("truncated chunk");
        }
        if (bytes_.substr(at_, expected.size()) != expected) {
            return fail(message);
        }
        at_ += expected.size();
        return true;
    }

    bool size_check(std::size_t expected, std::string_view type) {
        std::uint8_t b = 0;
        if (!byte(b)) {
            return false;
        }
        if (b != expected) {
            return fail(std::format("{} size mismatch", type));
        }
        return true;
    }

    bool header() {
        // (the first character, already checked, is part of the signature)
        if (!literal(signature, "not a binary chunk")) {
            return false;
        }
        std::uint8_t b = 0;
        if (!byte(b)) {
            return false;
        }
        if (b != version) {
            return fail("version mismatch");
        }
        if (!byte(b)) {
            return false;
        }
        if (b != format) {
            return fail("format mismatch");
        }
        if (!literal(check_data, "corrupted chunk") ||
            !size_check(sizeof(Instruction), "Instruction") ||
            !size_check(sizeof(std::int64_t), "lua_Integer") ||
            !size_check(sizeof(double), "lua_Number")) {
            return false;
        }
        std::int64_t i = 0;
        if (!raw(i)) {
            return false;
        }
        if (i != check_integer) {
            return fail("integer format mismatch");
        }
        double d = 0;
        if (!raw(d)) {
            return false;
        }
        if (d != check_number) {
            return fail("float format mismatch");
        }
        return true;
    }

    bool constants(Proto& f) {
        std::size_t n = 0;
        if (!size(n)) {
            return false;
        }
        f.constants.assign(n, Value::nil());
        for (Value& k : f.constants) {
            std::uint8_t t = 0;
            if (!byte(t)) {
                return false;
            }
            switch (t) {
            case tag_nil: break;
            case tag_false: k = Value::boolean(false); break;
            case tag_true: k = Value::boolean(true); break;
            case tag_float: {
                double d = 0;
                if (!raw(d)) {
                    return false;
                }
                k = Value::number(d);
                break;
            }
            case tag_integer: {
                std::int64_t i = 0;
                if (!raw(i)) {
                    return false;
                }
                k = Value::integer(i);
                break;
            }
            case tag_short_string:
            case tag_long_string: {
                const String* s = nullptr;
                if (!string(s)) {
                    return false;
                }
                if (s == nullptr) {
                    return fail("bad format for constant string");
                }
                k = Value::string(s);
                break;
            }
            default: return fail("bad format for constant");
            }
        }
        return true;
    }

    bool function(Proto& f, const String* parent_source) {
        const String* source = nullptr;
        if (!string(source)) {
            return false;
        }
        f.source = source != nullptr ? source : parent_source;
        std::uint8_t parameters = 0;
        std::uint8_t vararg = 0;
        std::uint8_t stack_size = 0;
        if (!integer(f.line_defined) || !integer(f.last_line_defined) || !byte(parameters) ||
            !byte(vararg) || !byte(stack_size)) {
            return false;
        }
        f.parameter_count = parameters;
        f.is_vararg = vararg != 0;
        f.max_stack_size = stack_size;
        std::size_t n = 0;
        if (!size(n)) {
            return false;
        }
        if (n > (bytes_.size() - at_) / sizeof(Instruction)) {
            return fail("truncated chunk");
        }
        f.code.resize(n);
        for (Instruction& i : f.code) {
            if (!raw(i)) {
                return false;
            }
        }
        if (!constants(f) || !size(n)) {
            return false;
        }
        f.upvalues.resize(std::min(n, bytes_.size() - at_));
        if (f.upvalues.size() != n) {
            return fail("truncated chunk");
        }
        for (Proto::UpvalueInfo& u : f.upvalues) {
            std::uint8_t in_stack = 0;
            if (!byte(in_stack) || !byte(u.index) || !byte(u.kind)) {
                return false;
            }
            u.in_stack = in_stack != 0;
        }
        if (!size(n)) {
            return false;
        }
        for (std::size_t j = 0; j < n; ++j) {
            Proto* p = heap_.new_proto();
            f.protos.push_back(p);
            if (!function(*p, f.source)) {
                return false;
            }
        }
        if (!debug(f)) {
            return false;
        }
        f.counted_bytes = f.vector_bytes();
        heap_.count_bytes(static_cast<std::ptrdiff_t>(f.counted_bytes));
        return true;
    }

    // loadDebug, with Lua's line encoding turned into absolute lines.
    bool debug(Proto& f) {
        std::size_t n = 0;
        if (!size(n)) {
            return false;
        }
        if (n > bytes_.size() - at_) {
            return fail("truncated chunk");
        }
        std::vector<std::int8_t> relative(n);
        for (std::int8_t& d : relative) {
            std::uint8_t b = 0;
            if (!byte(b)) {
                return false;
            }
            d = static_cast<std::int8_t>(b);
        }
        std::size_t absolute_count = 0;
        if (!size(absolute_count)) {
            return false;
        }
        std::vector<std::pair<int, int>> absolute(std::min(absolute_count, bytes_.size() - at_));
        if (absolute.size() != absolute_count) {
            return fail("truncated chunk");
        }
        for (auto& [pc, line] : absolute) {
            if (!integer(pc) || !integer(line)) {
                return false;
            }
        }
        f.lines.clear();
        if (relative.size() == f.code.size()) { // else no line information
            int line = f.line_defined;
            std::size_t next_absolute = 0;
            for (std::size_t pc = 0; pc < relative.size(); ++pc) {
                if (relative[pc] == abs_line_info) {
                    while (next_absolute < absolute.size() &&
                           std::cmp_less(absolute[next_absolute].first, pc)) {
                        ++next_absolute;
                    }
                    if (next_absolute < absolute.size()) {
                        line = absolute[next_absolute].second;
                    }
                } else {
                    line += relative[pc];
                }
                f.lines.push_back(line);
            }
        }
        if (!size(n)) {
            return false;
        }
        for (std::size_t j = 0; j < n; ++j) {
            Proto::LocalInfo local;
            if (!string(local.name) || !integer(local.start_pc) || !integer(local.end_pc)) {
                return false;
            }
            f.locals.push_back(local);
        }
        if (!size(n)) {
            return false;
        }
        if (n != 0) { // the upvalue names, one per upvalue
            for (Proto::UpvalueInfo& u : f.upvalues) {
                if (!string(u.name)) {
                    return false;
                }
            }
        }
        return true;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during the load
    Heap& heap_;
    std::string_view bytes_;
    std::size_t at_ = 0;
    std::string name_;
    std::string error_;
};

} // namespace

std::string dump(const Proto& main, bool strip) {
    Dumper d{strip};
    d.header();
    d.byte(static_cast<std::uint8_t>(main.upvalues.size()));
    d.function(main, nullptr);
    return d.take();
}

std::expected<LuaClosure*, std::string> undump(Heap& heap, std::string_view bytes,
                                               std::string_view source_name) {
    // luaU_undump: the chunk's name in messages.
    std::string name{source_name};
    if (name.starts_with('@') || name.starts_with('=')) {
        name.erase(0, 1);
    } else if (name.starts_with(signature.front())) {
        name = "binary string";
    }
    Loader loader{heap, bytes, name};
    Proto* main = nullptr;
    int upvalue_count = 0;
    if (!loader.load(main, upvalue_count)) {
        return std::unexpected(loader.error());
    }
    // Functions without a source (stripped) are named "=?", as Lua shows
    // them: "?" in messages.
    const String* unknown = heap.new_string("=?");
    std::vector<Proto*> pending{main};
    while (!pending.empty()) {
        Proto* p = pending.back();
        pending.pop_back();
        if (p->source == nullptr) {
            p->source = unknown;
        }
        pending.insert(pending.end(), p->protos.begin(), p->protos.end());
    }
    LuaClosure* closure = heap.new_closure(main, static_cast<std::size_t>(upvalue_count));
    for (Upvalue*& upvalue : closure->upvalues()) {
        upvalue = heap.new_upvalue(); // closed, nil (luaF_initupvals)
    }
    return closure;
}

} // namespace cpplua
