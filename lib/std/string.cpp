// The string library: a port of Lua 5.4.9's lstrlib.c
// (docs/final_docs/stdlib.md). Patterns, string.format, string.pack and the
// string metatable are Lua's, function by function; the names in comments are
// Lua's.

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/base.hpp"
#include "std/libs.hpp"
#include "value/arith.hpp"
#include "value/convert.hpp"
#include "value/locale.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/dump.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/metamethods.hpp"
#include "vm/run.hpp"
#include "vm/state.hpp"

namespace lua2 {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): byte-level string work

constexpr std::size_t max_size = INT_MAX; // MAXSIZE

unsigned char uchar(char c) {
    return static_cast<unsigned char>(c);
}

// posrelatI: a start position, negative from the end, clipped to [1, inf).
std::size_t start_position(std::int64_t pos, std::size_t len) {
    if (pos > 0) {
        return static_cast<std::size_t>(pos);
    }
    if (pos == 0) {
        return 1;
    }
    if (pos < -static_cast<std::int64_t>(len)) {
        return 1;
    }
    return len + static_cast<std::size_t>(pos) + 1;
}

// getendpos: an end position, negative from the end, clipped to [0, len].
std::size_t end_position(std::int64_t pos, std::size_t len) {
    if (std::cmp_greater(pos, len)) {
        return len;
    }
    if (pos >= 0) {
        return static_cast<std::size_t>(pos);
    }
    if (pos < -static_cast<std::int64_t>(len)) {
        return 0;
    }
    return len + static_cast<std::size_t>(pos) + 1;
}

int fail(State& state) {
    push(state, Value::nil());
    return 1;
}

// ---- Simple functions ----

int str_len(State& state) {
    std::string_view s;
    if (!check_view(state, 1, s)) {
        return c_function_error;
    }
    push(state, Value::integer(static_cast<std::int64_t>(s.size())));
    return 1;
}

int str_sub(State& state) {
    std::string_view s;
    std::int64_t i = 0;
    std::int64_t j = 0;
    if (!check_view(state, 1, s) || !check_integer(state, 2, i) || !opt_integer(state, 3, -1, j)) {
        return c_function_error;
    }
    const std::size_t start = start_position(i, s.size());
    const std::size_t end = end_position(j, s.size());
    push_string(state, start <= end ? s.substr(start - 1, end - start + 1) : std::string_view{});
    return 1;
}

int str_reverse(State& state) {
    std::string_view s;
    if (!check_view(state, 1, s)) {
        return c_function_error;
    }
    const std::string r{s.rbegin(), s.rend()};
    push_string(state, r);
    return 1;
}

template <int (*Convert)(int)>
int str_case(State& state) {
    std::string_view s;
    if (!check_view(state, 1, s)) {
        return c_function_error;
    }
    std::string r{s};
    for (char& c : r) {
        c = static_cast<char>(Convert(uchar(c)));
    }
    push_string(state, r);
    return 1;
}

int str_rep(State& state) {
    std::string_view s;
    std::int64_t n = 0;
    std::string_view sep;
    if (!check_view(state, 1, s) || !check_integer(state, 2, n) || !opt_view(state, 3, "", sep)) {
        return c_function_error;
    }
    if (n <= 0) {
        push_string(state, "");
        return 1;
    }
    if (s.size() + sep.size() < s.size() ||
        s.size() + sep.size() > max_size / static_cast<std::size_t>(n)) {
        return raise_error(state, "resulting string too large");
    }
    std::string r;
    r.reserve((static_cast<std::size_t>(n) * s.size()) +
              (static_cast<std::size_t>(n - 1) * sep.size()));
    while (n-- > 1) {
        r += s;
        r += sep;
    }
    r += s;
    push_string(state, r);
    return 1;
}

int str_byte(State& state) {
    std::string_view s;
    std::int64_t pi = 0;
    if (!check_view(state, 1, s) || !opt_integer(state, 2, 1, pi)) {
        return c_function_error;
    }
    std::int64_t pe = 0;
    if (!opt_integer(state, 3, pi, pe)) {
        return c_function_error;
    }
    const std::size_t posi = start_position(pi, s.size());
    const std::size_t pose = end_position(pe, s.size());
    if (posi > pose) {
        return 0;
    }
    if (pose - posi >= static_cast<std::size_t>(INT_MAX)) {
        return raise_error(state, "string slice too long");
    }
    const auto n = static_cast<int>(pose - posi) + 1;
    if (!state.check_stack(static_cast<std::uint32_t>(n))) {
        return raise_error(state, "stack overflow (string slice too long)");
    }
    for (int i = 0; i < n; ++i) {
        push(state, Value::integer(uchar(s[posi + static_cast<std::size_t>(i) - 1])));
    }
    return n;
}

int str_char(State& state) {
    const int n = arg_count(state);
    std::string r(static_cast<std::size_t>(n), '\0');
    for (int i = 1; i <= n; ++i) {
        std::int64_t c = 0;
        if (!check_integer(state, i, c)) {
            return c_function_error;
        }
        if (static_cast<std::uint64_t>(c) > UCHAR_MAX) {
            return arg_error(state, i, "value out of range");
        }
        r[static_cast<std::size_t>(i - 1)] = static_cast<char>(c);
    }
    push_string(state, r);
    return 1;
}

int str_dump(State& state) {
    // A Lua function as a binary chunk, in Lua 5.4's format (vm/dump.hpp).
    const bool strip = arg(state, 2).is_truthy();
    if (!check_function(state, 1)) {
        return c_function_error;
    }
    const Value f = arg(state, 1);
    if (!f.is_closure()) {
        return raise_error(state, "unable to dump given function");
    }
    push_string(state, dump(*f.as_closure()->proto, strip));
    return 1;
}

// ---- The metatable's arithmetic (tonum, trymt, arith) ----

std::optional<Value> to_number_value(Value v) {
    if (v.is_number()) {
        return v;
    }
    if (v.is_string()) {
        return string_to_number(v.as_string()->view());
    }
    return std::nullopt;
}

template <Event E>
int string_arith(State& state) {
    const Value a = arg(state, 1);
    const Value b = arg(state, 2);
    const auto x = to_number_value(a);
    const auto y = to_number_value(b);
    if (x && y) {
        ArithResult r;
        switch (E) {
        case Event::Add: r = add(*x, *y); break;
        case Event::Sub: r = subtract(*x, *y); break;
        case Event::Mul: r = multiply(*x, *y); break;
        case Event::Mod: r = modulo(*x, *y); break;
        case Event::Pow: r = power(*x, *y); break;
        case Event::Div: r = divide(*x, *y); break;
        case Event::IDiv: r = floor_divide(*x, *y); break;
        default: r = negate(*x); break;
        }
        if (!r) {
            state.set_error(r.error().message); // lua_arith: raised in C, no position
            return c_function_error;
        }
        push(state, *r);
        return 1;
    }
    // trymt: the second operand's metamethod, unless it is a string.
    const std::string_view name = event_name(E);
    const Value tm = b.is_string() ? Value::nil() : metafield(state.heap(), b, name);
    if (tm.is_nil()) {
        return raise_error(state, std::format("attempt to {} a '{}' with a '{}'", name.substr(2),
                                              type_name(a.type()), type_name(b.type())));
    }
    Value r;
    if (call_metamethod(state, tm, a, b, r) != Status::Ok) {
        return c_function_error;
    }
    push(state, r);
    return 1;
}

// ==== Pattern matching ====

constexpr int max_captures = 32;
constexpr int cap_unfinished = -1;
constexpr int cap_position = -2;
constexpr int max_match_depth = 200; // MAXCCALLS
constexpr char escape = '%';
constexpr std::string_view specials = "^$*+?.([%-";

// MatchState. An error while matching (a malformed pattern) is recorded
// and every level returns nullptr, so the error leaves the matcher like
// Lua's longjmp, then is raised.
struct MatchState {
    const char* src_init = nullptr;
    const char* src_end = nullptr;
    const char* p_end = nullptr;
    int match_depth = 0;
    int level = 0;

    struct Capture {
        const char* init = nullptr;
        std::ptrdiff_t len = 0;
    };

    std::array<Capture, max_captures> capture{};
    std::string error; // the error, when failed
    bool failed = false;

    const char* fail(std::string message) {
        if (!failed) {
            failed = true;
            error = std::move(message);
        }
        return nullptr;
    }
};

const char* match(MatchState& ms, const char* s, const char* p);

int check_capture(MatchState& ms, int l) {
    l -= '1';
    if (l < 0 || l >= ms.level || ms.capture[static_cast<std::size_t>(l)].len == cap_unfinished) {
        ms.fail(std::format("invalid capture index %{}", l + 1));
        return -1;
    }
    return l;
}

int capture_to_close(MatchState& ms) {
    for (int level = ms.level - 1; level >= 0; --level) {
        if (ms.capture[static_cast<std::size_t>(level)].len == cap_unfinished) {
            return level;
        }
    }
    ms.fail("invalid pattern capture");
    return -1;
}

const char* class_end(MatchState& ms, const char* p) {
    switch (*p++) {
    case escape:
        if (p == ms.p_end) {
            return ms.fail("malformed pattern (ends with '%')");
        }
        return p + 1;
    case '[':
        if (*p == '^') {
            ++p;
        }
        do { // look for a ']'
            if (p == ms.p_end) {
                return ms.fail("malformed pattern (missing ']')");
            }
            const char c = *p;
            ++p;
            if (c == escape && p < ms.p_end) {
                ++p; // skip escapes (e.g. '%]')
            }
        } while (*p != ']');
        return p + 1;
    default: return p;
    }
}

// The character classes of the C locale, the one Lua starts in, as a
// table: the C library's isalpha and the rest are calls into it.
constexpr std::uint16_t class_alpha = 1U << 0U;
constexpr std::uint16_t class_cntrl = 1U << 1U;
constexpr std::uint16_t class_digit = 1U << 2U;
constexpr std::uint16_t class_graph = 1U << 3U;
constexpr std::uint16_t class_lower = 1U << 4U;
constexpr std::uint16_t class_punct = 1U << 5U;
constexpr std::uint16_t class_space = 1U << 6U;
constexpr std::uint16_t class_upper = 1U << 7U;
constexpr std::uint16_t class_xdigit = 1U << 8U;

constexpr std::array<std::uint16_t, 256> char_classes = [] {
    std::array<std::uint16_t, 256> t{};
    for (int c = 0; c < 128; ++c) {
        std::uint16_t bits = 0;
        const bool lower = c >= 'a' && c <= 'z';
        const bool upper = c >= 'A' && c <= 'Z';
        const bool digit = c >= '0' && c <= '9';
        const bool graph = c > ' ' && c < 127;
        bits |= lower || upper ? class_alpha : 0U;
        bits |= c < ' ' || c == 127 ? class_cntrl : 0U;
        bits |= digit ? class_digit : 0U;
        bits |= graph ? class_graph : 0U;
        bits |= lower ? class_lower : 0U;
        bits |= graph && !lower && !upper && !digit ? class_punct : 0U;
        bits |= c == ' ' || (c >= '\t' && c <= '\r') ? class_space : 0U;
        bits |= upper ? class_upper : 0U;
        bits |= digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ? class_xdigit : 0U;
        t.at(static_cast<std::size_t>(c)) = bits;
    }
    return t;
}();

bool has_class(int c, std::uint16_t bits) {
    return (char_classes[static_cast<std::size_t>(c) & 0xFFU] & bits) != 0;
}

// match_class in another LC_CTYPE: the C library's classes, as Lua's.
bool match_class_locale(int c, int cl) {
    bool res = false;
    switch (std::tolower(cl)) {
    case 'a': res = std::isalpha(c) != 0; break;
    case 'c': res = std::iscntrl(c) != 0; break;
    case 'd': res = std::isdigit(c) != 0; break;
    case 'g': res = std::isgraph(c) != 0; break;
    case 'l': res = std::islower(c) != 0; break;
    case 'p': res = std::ispunct(c) != 0; break;
    case 's': res = std::isspace(c) != 0; break;
    case 'u': res = std::isupper(c) != 0; break;
    case 'w': res = std::isalnum(c) != 0; break;
    case 'x': res = std::isxdigit(c) != 0; break;
    case 'z': res = c == 0; break; // deprecated option
    default: return cl == c;
    }
    return std::islower(cl) != 0 ? res : !res;
}

bool match_class(int c, int cl) {
    if (!ctype_is_c()) [[unlikely]] {
        return match_class_locale(c, cl);
    }
    bool res = false;
    switch (static_cast<unsigned int>(cl) | 0x20U) { // tolower for letters
    case 'a': res = has_class(c, class_alpha); break;
    case 'c': res = has_class(c, class_cntrl); break;
    case 'd': res = has_class(c, class_digit); break;
    case 'g': res = has_class(c, class_graph); break;
    case 'l': res = has_class(c, class_lower); break;
    case 'p': res = has_class(c, class_punct); break;
    case 's': res = has_class(c, class_space); break;
    case 'u': res = has_class(c, class_upper); break;
    case 'w': res = has_class(c, class_alpha | class_digit); break;
    case 'x': res = has_class(c, class_xdigit); break;
    case 'z': res = c == 0; break; // deprecated option
    default: return cl == c;
    }
    return has_class(cl, class_lower) ? res : !res;
}

bool match_bracket_class(int c, const char* p, const char* ec) {
    bool sig = true;
    if (*(p + 1) == '^') {
        sig = false;
        ++p;
    }
    while (++p < ec) {
        if (*p == escape) {
            ++p;
            if (match_class(c, uchar(*p))) {
                return sig;
            }
        } else if (*(p + 1) == '-' && p + 2 < ec) {
            p += 2;
            if (std::cmp_less_equal(uchar(*(p - 2)), c) && std::cmp_less_equal(c, uchar(*p))) {
                return sig;
            }
        } else if (std::cmp_equal(uchar(*p), c)) {
            return sig;
        }
    }
    return !sig;
}

bool single_match(const MatchState& ms, const char* s, const char* p, const char* ep) {
    if (s >= ms.src_end) {
        return false;
    }
    const int c = uchar(*s);
    switch (*p) {
    case '.': return true;
    case escape: return match_class(c, uchar(*(p + 1)));
    case '[': return match_bracket_class(c, p, ep - 1);
    default: return std::cmp_equal(uchar(*p), c);
    }
}

const char* match_balance(MatchState& ms, const char* s, const char* p) {
    if (p >= ms.p_end - 1) {
        return ms.fail("malformed pattern (missing arguments to '%b')");
    }
    if (*s != *p) {
        return nullptr;
    }
    const char b = *p;
    const char e = *(p + 1);
    int cont = 1;
    while (++s < ms.src_end) {
        if (*s == e) {
            if (--cont == 0) {
                return s + 1;
            }
        } else if (*s == b) {
            ++cont;
        }
    }
    return nullptr;
}

const char* max_expand(MatchState& ms, const char* s, const char* p, const char* ep) {
    std::ptrdiff_t i = 0;
    while (single_match(ms, s + i, p, ep)) {
        ++i;
    }
    while (i >= 0) { // try with the most repetitions first
        const char* res = match(ms, s + i, ep + 1);
        if (res != nullptr || ms.failed) {
            return res;
        }
        --i;
    }
    return nullptr;
}

const char* min_expand(MatchState& ms, const char* s, const char* p, const char* ep) {
    for (;;) {
        const char* res = match(ms, s, ep + 1);
        if (res != nullptr || ms.failed) {
            return res;
        }
        if (single_match(ms, s, p, ep)) {
            ++s;
        } else {
            return nullptr;
        }
    }
}

const char* start_capture(MatchState& ms, const char* s, const char* p, int what) {
    if (ms.level >= max_captures) {
        return ms.fail("too many captures");
    }
    ms.capture[static_cast<std::size_t>(ms.level)] = {.init = s, .len = what};
    ++ms.level;
    const char* res = match(ms, s, p);
    if (res == nullptr) {
        --ms.level; // undo capture
    }
    return res;
}

const char* end_capture(MatchState& ms, const char* s, const char* p) {
    const int l = capture_to_close(ms);
    if (l < 0) {
        return nullptr;
    }
    auto& cap = ms.capture[static_cast<std::size_t>(l)];
    cap.len = s - cap.init;
    const char* res = match(ms, s, p);
    if (res == nullptr) {
        cap.len = cap_unfinished; // undo capture
    }
    return res;
}

const char* match_capture(MatchState& ms, const char* s, int l) {
    l = check_capture(ms, l);
    if (l < 0) {
        return nullptr;
    }
    const auto& cap = ms.capture[static_cast<std::size_t>(l)];
    const auto len = static_cast<std::size_t>(cap.len);
    if (std::cmp_greater_equal(ms.src_end - s, len) && std::memcmp(cap.init, s, len) == 0) {
        return s + len;
    }
    return nullptr;
}

const char* match(MatchState& ms, const char* s, const char* p) {
    if (ms.match_depth-- == 0) {
        return ms.fail("pattern too complex");
    }
    for (;;) { // a loop instead of tail recursion ('goto init')
        if (p == ms.p_end) {
            break;
        }
        bool next = false; // continue with the new s and p
        switch (*p) {
        case '(':
            s = *(p + 1) == ')' ? start_capture(ms, s, p + 2, cap_position)
                                : start_capture(ms, s, p + 1, cap_unfinished);
            break;
        case ')': s = end_capture(ms, s, p + 1); break;
        case '$':
            if (p + 1 != ms.p_end) {
                goto dflt; // NOLINT(cppcoreguidelines-avoid-goto): Lua's own structure
            }
            s = s == ms.src_end ? s : nullptr;
            break;
        case escape:
            switch (*(p + 1)) {
            case 'b':
                s = match_balance(ms, s, p + 2);
                if (s != nullptr) {
                    p += 4;
                    next = true;
                }
                break;
            case 'f': {
                p += 2;
                if (*p != '[') {
                    s = ms.fail("missing '[' after '%f' in pattern");
                    break;
                }
                const char* ep = class_end(ms, p);
                if (ep == nullptr) {
                    s = nullptr;
                    break;
                }
                const char previous = s == ms.src_init ? '\0' : *(s - 1);
                if (!match_bracket_class(uchar(previous), p, ep - 1) &&
                    match_bracket_class(uchar(*s), p, ep - 1)) {
                    p = ep;
                    next = true;
                } else {
                    s = nullptr;
                }
                break;
            }
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                s = match_capture(ms, s, uchar(*(p + 1)));
                if (s != nullptr) {
                    p += 2;
                    next = true;
                }
                break;
            default: goto dflt; // NOLINT(cppcoreguidelines-avoid-goto)
            }
            break;
        default:
        dflt:
            { // a single class with an optional suffix
                const char* ep = class_end(ms, p);
                if (ep == nullptr) {
                    s = nullptr;
                    break;
                }
                if (!single_match(ms, s, p, ep)) {
                    if (*ep == '*' || *ep == '?' || *ep == '-') { // accept empty
                        p = ep + 1;
                        next = true;
                    } else {
                        s = nullptr;
                    }
                } else {
                    switch (*ep) {
                    case '?': {
                        const char* res = match(ms, s + 1, ep + 1);
                        if (res != nullptr || ms.failed) {
                            s = res;
                        } else {
                            p = ep + 1;
                            next = true;
                        }
                        break;
                    }
                    case '+': s = max_expand(ms, s + 1, p, ep); break;
                    case '*': s = max_expand(ms, s, p, ep); break;
                    case '-': s = min_expand(ms, s, p, ep); break;
                    default:
                        ++s;
                        p = ep;
                        next = true;
                    }
                }
                break;
            }
        }
        if (!next) {
            break;
        }
    }
    ++ms.match_depth;
    return !ms.failed ? s : nullptr;
}

// lmemfind
const char* find_plain(const char* s1, std::size_t l1, const char* s2, std::size_t l2) {
    if (l2 == 0) {
        return s1;
    }
    if (l2 > l1) {
        return nullptr;
    }
    const std::string_view haystack{s1, l1};
    const std::size_t at = haystack.find(std::string_view{s2, l2});
    return at == std::string_view::npos ? nullptr : s1 + at;
}

// get_onecapture: capture i as a string, or its position (pushed as an
// integer: the result is then cap_position).
std::ptrdiff_t get_one_capture(State& state, MatchState& ms, int i, const char* s, const char* e,
                               const char*& cap) {
    if (i >= ms.level) {
        if (i != 0) {
            ms.fail(std::format("invalid capture index %{}", i + 1));
            return 0;
        }
        cap = s;
        // The whole match. A null s (find's captures) comes with captures
        // (push_captures), so i < level and this is not reached with it.
        return s != nullptr ? e - s : 0;
    }
    const auto& c = ms.capture[static_cast<std::size_t>(i)];
    cap = c.init;
    if (c.len == cap_unfinished) {
        ms.fail("unfinished capture");
        return 0;
    }
    if (c.len == cap_position) {
        push(state, Value::integer((c.init - ms.src_init) + 1));
    }
    return c.len;
}

void push_one_capture(State& state, MatchState& ms, int i, const char* s, const char* e) {
    const char* cap = nullptr;
    const std::ptrdiff_t l = get_one_capture(state, ms, i, s, e, cap);
    if (!ms.failed && l != cap_position) {
        push_string(state, std::string_view{cap, static_cast<std::size_t>(l)});
    }
}

// push_captures: how many values, or -1 after raising an error.
int push_captures(State& state, MatchState& ms, const char* s, const char* e) {
    const int levels = ms.level == 0 && s != nullptr ? 1 : ms.level;
    if (!state.check_stack(static_cast<std::uint32_t>(levels))) {
        raise_error(state, "too many captures");
        return -1;
    }
    for (int i = 0; i < levels; ++i) {
        push_one_capture(state, ms, i, s, e);
        if (ms.failed) {
            raise_error(state, ms.error);
            return -1;
        }
    }
    return levels;
}

bool no_specials(std::string_view p) {
    return p.find_first_of(specials) == std::string_view::npos;
}

void prepare(MatchState& ms, std::string_view s, std::string_view p) {
    ms.src_init = s.data();
    ms.src_end = s.data() + s.size();
    ms.p_end = p.data() + p.size();
}

void reprepare(MatchState& ms) {
    ms.match_depth = max_match_depth;
    ms.level = 0;
}

int find_aux(State& state, bool find) {
    std::string_view s;
    std::string_view p;
    std::int64_t init_arg = 0;
    if (!check_view(state, 1, s) || !check_view(state, 2, p) ||
        !opt_integer(state, 3, 1, init_arg)) {
        return c_function_error;
    }
    const std::size_t init = start_position(init_arg, s.size()) - 1;
    if (init > s.size()) {
        return fail(state);
    }
    if (find && (arg(state, 4).is_truthy() || no_specials(p))) {
        if (const char* s2 = find_plain(s.data() + init, s.size() - init, p.data(), p.size())) {
            push(state, Value::integer((s2 - s.data()) + 1));
            push(state, Value::integer(static_cast<std::int64_t>(
                            (s2 - s.data()) + static_cast<std::ptrdiff_t>(p.size()))));
            return 2;
        }
        return fail(state);
    }
    MatchState ms;
    const char* s1 = s.data() + init;
    const bool anchor = !p.empty() && p.front() == '^';
    if (anchor) {
        p.remove_prefix(1);
    }
    prepare(ms, s, p);
    do {
        reprepare(ms);
        const char* res =
            match(ms, s1,
                  p.data()); // NOLINT(bugprone-suspicious-stringview-data-usage): ends at ms.p_end
        if (ms.failed) {
            return raise_error(state, ms.error);
        }
        if (res != nullptr) {
            if (find) {
                push(state, Value::integer((s1 - s.data()) + 1));
                push(state, Value::integer(res - s.data()));
                const int n = push_captures(state, ms, nullptr, nullptr);
                return n < 0 ? c_function_error : n + 2;
            }
            const int n = push_captures(state, ms, s1, res);
            return n < 0 ? c_function_error : n;
        }
    } while (s1++ < ms.src_end && !anchor);
    return fail(state);
}

int str_find(State& state) {
    return find_aux(state, true);
}

int str_match(State& state) {
    return find_aux(state, false);
}

// The state of a gmatch iterator, in a userdata (its upvalue 3; the
// subject and pattern strings are upvalues 1 and 2, kept alive).
struct GMatchState {
    const char* src = nullptr;
    const char* p = nullptr;
    const char* last_match = nullptr;
    MatchState ms;
};

int gmatch_aux(State& state) {
    const auto* closure = state.get(state.ci->func).as_c_closure();
    auto* gm = static_cast<GMatchState*>(closure->upvalues()[2].as_userdata()->data());
    for (const char* src = gm->src; src <= gm->ms.src_end; ++src) {
        reprepare(gm->ms);
        const char* e = match(gm->ms, src, gm->p);
        if (gm->ms.failed) {
            return raise_error(state, gm->ms.error);
        }
        if (e != nullptr && e != gm->last_match) {
            gm->src = gm->last_match = e;
            const int n = push_captures(state, gm->ms, src, e);
            return n < 0 ? c_function_error : n;
        }
    }
    return 0;
}

int str_gmatch(State& state) {
    std::string_view s;
    std::string_view p;
    std::int64_t init_arg = 0;
    if (!check_view(state, 1, s) || !check_view(state, 2, p) ||
        !opt_integer(state, 3, 1, init_arg)) {
        return c_function_error;
    }
    std::size_t init = start_position(init_arg, s.size()) - 1;
    if (init > s.size()) {
        init = s.size() + 1;
    }
    Heap& heap = state.heap();
    Userdata* box = heap.new_userdata(sizeof(GMatchState), 0);
    auto* gm = new (box->data()) GMatchState{}; // NOLINT(cppcoreguidelines-owning-memory)
    box->destructor = [](void* data) { static_cast<GMatchState*>(data)->~GMatchState(); };
    prepare(gm->ms, s, p);
    gm->src = s.data() + init;
    gm->p = p.data();
    CClosure* closure = heap.new_c_closure(&gmatch_aux, 3);
    closure->upvalues()[0] = arg(state, 1);
    closure->upvalues()[1] = arg(state, 2);
    closure->upvalues()[2] = Value::userdata(box);
    push(state, Value::c_closure(closure));
    return 1;
}

// add_s: the replacement string with %0-%9 and %%.
bool add_string(State& state, MatchState& ms, std::string& b, std::string_view news, const char* s,
                const char* e) {
    std::size_t at = 0;
    while (at < news.size()) {
        const std::size_t esc = news.find(escape, at);
        if (esc == std::string_view::npos) {
            break;
        }
        b.append(news.substr(at, esc - at));
        const std::size_t c = esc + 1;
        const char ch = c < news.size() ? news[c] : '\0';
        if (ch == escape) {
            b += escape;
        } else if (ch == '0') {
            b.append(s, static_cast<std::size_t>(e - s));
        } else if (std::isdigit(uchar(ch)) != 0) {
            const char* cap = nullptr;
            const std::uint32_t top = state.top;
            const std::ptrdiff_t l = get_one_capture(state, ms, ch - '1', s, e, cap);
            if (ms.failed) {
                raise_error(state, ms.error);
                return false;
            }
            if (l == cap_position) {
                b += display_text(state.get(state.top - 1));
                state.top = top;
            } else {
                b.append(cap, static_cast<std::size_t>(l));
            }
        } else {
            raise_error(state, "invalid use of '%' in replacement string");
            return false;
        }
        at = c + 1;
    }
    b.append(news.substr(std::min(at, news.size())));
    return true;
}

// add_value: whether the match was replaced; nullopt after an error.
std::optional<bool> add_value(State& state, MatchState& ms, std::string& b, const char* s,
                              const char* e, Type tr) {
    const std::uint32_t top = state.top;
    switch (tr) {
    case Type::Function: {
        const std::uint32_t func = state.top;
        push(state, arg(state, 3));
        if (push_captures(state, ms, s, e) < 0 || call(state, func, 1) != Status::Ok) {
            return std::nullopt;
        }
        break;
    }
    case Type::Table: {
        push_one_capture(state, ms, 0, s, e);
        if (ms.failed) {
            raise_error(state, ms.error);
            return std::nullopt;
        }
        Value v;
        if (index_value(state, arg(state, 3), state.get(state.top - 1), v) != Status::Ok) {
            return std::nullopt;
        }
        state.set(state.top - 1, v);
        break;
    }
    default: {
        std::string_view news;
        if (!check_view(state, 3, news) || !add_string(state, ms, b, news, s, e)) {
            return std::nullopt;
        }
        return true;
    }
    }
    const Value r = state.get(state.top - 1);
    state.top = top;
    if (r.is_falsy()) { // keep the original text
        b.append(s, static_cast<std::size_t>(e - s));
        return false;
    }
    if (!r.is_string() && !r.is_number()) {
        raise_error(state, std::format("invalid replacement value (a {})", type_name(r.type())));
        return std::nullopt;
    }
    b += r.is_string() ? std::string{r.as_string()->view()} : display_text(r);
    return true;
}

int str_gsub(State& state) {
    std::string_view src_view;
    std::string_view p;
    if (!check_view(state, 1, src_view) || !check_view(state, 2, p)) {
        return c_function_error;
    }
    const Type tr = arg(state, 3).type();
    std::int64_t max_s = 0;
    if (!opt_integer(state, 4, static_cast<std::int64_t>(src_view.size()) + 1, max_s)) {
        return c_function_error;
    }
    if (tr != Type::Number && tr != Type::String && tr != Type::Function && tr != Type::Table) {
        return arg_type_error(state, 3, "string/function/table");
    }
    const bool anchor = !p.empty() && p.front() == '^';
    if (anchor) {
        p.remove_prefix(1);
    }
    MatchState ms;
    prepare(ms, src_view, p);
    const char* src = src_view.data();
    const char* last_match = nullptr;
    std::int64_t n = 0;
    bool changed = false;
    std::string b;
    const char* copied = src; // the text before src not yet in b
    while (n < max_s) {
        reprepare(ms);
        const char* e =
            match(ms, src,
                  p.data()); // NOLINT(bugprone-suspicious-stringview-data-usage): ends at ms.p_end
        if (ms.failed) {
            return raise_error(state, ms.error);
        }
        if (e != nullptr && e != last_match) {
            ++n;
            b.append(copied, static_cast<std::size_t>(src - copied));
            const auto added = add_value(state, ms, b, src, e, tr);
            if (!added) {
                return c_function_error;
            }
            changed = *added || changed;
            src = last_match = e;
            copied = src;
        } else if (src < ms.src_end) {
            ++src; // kept as it is
        } else {
            break;
        }
        if (anchor) {
            break;
        }
    }
    if (!changed) {
        push(state, arg(state, 1));
    } else {
        b.append(copied, static_cast<std::size_t>(ms.src_end - copied));
        push_string(state, b);
    }
    push(state, Value::integer(n));
    return 2;
}

// ==== string.format ====

constexpr std::string_view flags_float = "-+#0 ";
constexpr std::string_view flags_x = "-#0";
constexpr std::string_view flags_i = "-+0 ";
constexpr std::string_view flags_u = "-0";
constexpr std::string_view flags_c = "-";

// snprintf with a format built at run time, as Lua's l_sprintf. On MinGW
// it is MinGW's own C99 one, as the C library's prints NaN as "-nan(ind)"
// and %a with every digit; so numbers come out as on other systems.
template <class... T>
int c_format(char* buffer, std::size_t size, const char* form, T... value) {
#ifdef __MINGW32__
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return __mingw_snprintf(buffer, size, form, value...);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return std::snprintf(buffer, size, form, value...);
#endif
}

template <class T>
std::string format_one(const std::string& form, T value) {
    std::array<char, 512> buffer{};
    const int n = c_format(buffer.data(), buffer.size(), form.c_str(), value);
    if (n < 0) {
        return {};
    }
    if (static_cast<std::size_t>(n) < buffer.size()) {
        return std::string{buffer.data(), static_cast<std::size_t>(n)};
    }
    std::string big(static_cast<std::size_t>(n) + 1, '\0');
    c_format(big.data(), big.size(), form.c_str(), value);
    big.resize(static_cast<std::size_t>(n));
    return big;
}

void add_quoted(std::string& b, std::string_view s) {
    b += '"';
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\' || c == '\n') {
            b += '\\';
            b += c;
        } else if (std::iscntrl(uchar(c)) != 0) {
            const bool digit_next = i + 1 < s.size() && std::isdigit(uchar(s[i + 1])) != 0;
            b += digit_next ? std::format("\\{:03}", uchar(c)) : std::format("\\{}", uchar(c));
        } else {
            b += c;
        }
    }
    b += '"';
}

// checkformat: flags, a width of up to 2 digits, maybe a precision.
bool check_format(State& state, const std::string& form, std::string_view flags, bool precision) {
    std::size_t at = 1;
    while (at < form.size() && flags.contains(form[at])) {
        ++at;
    }
    const auto two_digits = [&] {
        if (at < form.size() && std::isdigit(uchar(form[at])) != 0) {
            ++at;
            if (at < form.size() && std::isdigit(uchar(form[at])) != 0) {
                ++at;
            }
        }
    };
    if (at < form.size() && form[at] != '0') {
        two_digits();
        if (at < form.size() && form[at] == '.' && precision) {
            ++at;
            two_digits();
        }
    }
    if (at >= form.size() || std::isalpha(uchar(form[at])) == 0) {
        raise_error(state, std::format("invalid conversion specification: '{}'", form));
        return false;
    }
    return true;
}

// The format with a length modifier before its conversion (addlenmod).
std::string with_length(const std::string& form, std::string_view modifier) {
    return form.substr(0, form.size() - 1) + std::string{modifier} + form.back();
}

int str_format(State& state) {
    const int top = arg_count(state);
    int arg_n = 1;
    std::string_view fmt;
    if (!check_view(state, 1, fmt)) {
        return c_function_error;
    }
    std::string b;
    std::size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] != escape) {
            b += fmt[i++];
            continue;
        }
        ++i;
        if (i < fmt.size() && fmt[i] == escape) {
            b += fmt[i++];
            continue;
        }
        if (++arg_n > top) {
            return arg_error(state, arg_n, "no value");
        }
        // getformat: flags, width and precision, then the conversion.
        std::size_t len = 0;
        constexpr std::string_view spec_chars = "-+#0 123456789.";
        while (i + len < fmt.size() && spec_chars.contains(fmt[i + len])) {
            ++len;
        }
        ++len; // the conversion
        if (len >= 32 - 10) {
            return raise_error(state, "invalid format (too long)");
        }
        std::string form = "%" + std::string{fmt.substr(i, std::min(len, fmt.size() - i))};
        i += len;
        const char conversion = form.size() > 1 ? form.back() : '\0';
        switch (conversion) {
        case 'c': {
            std::int64_t c = 0;
            if (!check_format(state, form, flags_c, false) || !check_integer(state, arg_n, c)) {
                return c_function_error;
            }
            b += format_one(form, static_cast<int>(c));
            break;
        }
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            std::int64_t n = 0;
            if (!check_integer(state, arg_n, n)) {
                return c_function_error;
            }
            std::string_view flags = flags_x;
            if (conversion == 'd' || conversion == 'i') {
                flags = flags_i;
            } else if (conversion == 'u') {
                flags = flags_u;
            }
            if (!check_format(state, form, flags, true)) {
                return c_function_error;
            }
            b += format_one(with_length(form, "ll"), static_cast<long long>(n));
            break;
        }
        case 'a':
        case 'A':
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            if (conversion == 'F') {
                return raise_error(state, std::format("invalid conversion '{}' to 'format'", form));
            }
            double n = 0;
            if (!check_number(state, arg_n, n) || !check_format(state, form, flags_float, true)) {
                return c_function_error;
            }
            b += format_one(form, n);
            break;
        }
        case 'p': {
            if (!check_format(state, form, flags_c, false)) {
                return c_function_error;
            }
            const Value v = arg(state, arg_n);
            if (!v.is_collectable() && !v.is_light_userdata() && !v.is_c_function()) {
                form.back() = 's';
                b += format_one(form, "(null)");
            } else {
                b += format_one(form, std::bit_cast<const void*>(v.payload()));
            }
            break;
        }
        case 'q': {
            if (form.size() != 2) {
                return raise_error(state, "specifier '%q' cannot have modifiers");
            }
            const Value v = arg(state, arg_n);
            if (v.is_string()) {
                add_quoted(b, v.as_string()->view());
            } else if (v.is_integer()) {
                const std::int64_t n = v.as_integer();
                b += n == std::numeric_limits<std::int64_t>::min()
                         ? format_one(std::string{"0x%llx"}, static_cast<long long>(n))
                         : std::format("{}", n);
            } else if (v.is_float()) {
                const double n = v.as_float();
                if (n == std::numeric_limits<double>::infinity()) {
                    b += "1e9999";
                } else if (n == -std::numeric_limits<double>::infinity()) {
                    b += "-1e9999";
                } else if (n != n) {
                    b += "(0/0)";
                } else {
                    // quotefloat: with a dot, whatever the locale's decimal point
                    std::string text = format_one(std::string{"%a"}, n);
                    if (!text.contains('.')) {
                        if (const std::size_t at = text.find(locale_decimal_point());
                            at != std::string::npos) {
                            text[at] = '.';
                        }
                    }
                    b += text;
                }
            } else if (v.is_nil() || v.is_boolean()) {
                b += display_text(v);
            } else {
                return arg_error(state, arg_n, "value has no literal form");
            }
            break;
        }
        case 's': {
            Value text;
            if (to_display_string(state, arg(state, arg_n), text) != Status::Ok) {
                return c_function_error;
            }
            const std::string_view s = text.as_string()->view();
            if (form.size() == 2) {
                b += s;
            } else {
                if (s.contains('\0')) {
                    return arg_error(state, arg_n, "string contains zeros");
                }
                if (!check_format(state, form, flags_c, true)) {
                    return c_function_error;
                }
                if (!form.contains('.') && s.size() >= 100) {
                    b += s; // too long to format, kept whole
                } else {
                    b += format_one(form, std::string{s}.c_str());
                }
            }
            break;
        }
        default:
            return raise_error(state, std::format("invalid conversion '{}' to 'format'", form));
        }
    }
    push_string(state, b);
    return 1;
}

// ==== string.pack, string.unpack, string.packsize ====

constexpr int max_int_size = 16;
constexpr int size_lua_integer = 8;

enum class Option : std::uint8_t {
    Int,
    Uint,
    Float,
    Number,
    Double,
    Char,
    String,
    Zstr,
    Padding,
    PaddAlign,
    Nop,
};

struct Header {
    bool little = true; // the machine is little-endian
    int max_align = 1;
    std::string error;
};

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

int get_num(std::string_view fmt, std::size_t& at, int df) {
    if (at >= fmt.size() || !is_digit(fmt[at])) {
        return df;
    }
    int a = 0;
    do {
        a = (a * 10) + (fmt[at++] - '0');
    } while (at < fmt.size() && is_digit(fmt[at]) && a <= (static_cast<int>(max_size) - 9) / 10);
    return a;
}

int get_num_limit(Header& h, std::string_view fmt, std::size_t& at, int df) {
    const int size = get_num(fmt, at, df);
    if (size > max_int_size || size <= 0) {
        h.error = std::format("integral size ({}) out of limits [1,{}]", size, max_int_size);
        return 1;
    }
    return size;
}

Option get_option(Header& h, std::string_view fmt, std::size_t& at, int& size) {
    const char opt = fmt[at++];
    size = 0;
    switch (opt) {
    case 'b': size = 1; return Option::Int;
    case 'B': size = 1; return Option::Uint;
    case 'h': size = 2; return Option::Int;
    case 'H': size = 2; return Option::Uint;
    case 'l': size = static_cast<int>(sizeof(long)); return Option::Int;
    case 'L': size = static_cast<int>(sizeof(long)); return Option::Uint;
    case 'j': size = 8; return Option::Int;
    case 'J': size = 8; return Option::Uint;
    case 'T': size = static_cast<int>(sizeof(std::size_t)); return Option::Uint;
    case 'f': size = 4; return Option::Float;
    case 'n': size = 8; return Option::Number;
    case 'd': size = 8; return Option::Double;
    case 'i': size = get_num_limit(h, fmt, at, 4); return Option::Int;
    case 'I': size = get_num_limit(h, fmt, at, 4); return Option::Uint;
    case 's':
        size = get_num_limit(h, fmt, at, static_cast<int>(sizeof(std::size_t)));
        return Option::String;
    case 'c':
        size = get_num(fmt, at, -1);
        if (size == -1) {
            h.error = "missing size for format option 'c'";
        }
        return Option::Char;
    case 'z': return Option::Zstr;
    case 'x': size = 1; return Option::Padding;
    case 'X': return Option::PaddAlign;
    case ' ': break;
    case '<': h.little = true; break;
    case '>': h.little = false; break;
    case '=': h.little = true; break; // native: little-endian
    case '!': h.max_align = get_num_limit(h, fmt, at, 8); break;
    default: h.error = std::format("invalid format option '{}'", opt); break;
    }
    return Option::Nop;
}

// getdetails: the option, its size, and the padding to align it. An
// argument error (option 'X', alignment) goes to `arg_error_text`.
Option get_details(Header& h, std::size_t total, std::string_view fmt, std::size_t& at, int& size,
                   int& to_align, std::string& arg_error_text) {
    const Option opt = get_option(h, fmt, at, size);
    int align = size;
    if (opt == Option::PaddAlign &&
        (at >= fmt.size() || get_option(h, fmt, at, align) == Option::Char || align == 0)) {
        arg_error_text = "invalid next option for option 'X'";
    }
    if (align <= 1 || opt == Option::Char) {
        to_align = 0;
    } else {
        align = std::min(align, h.max_align);
        if (!std::has_single_bit(static_cast<unsigned int>(align))) { // align > 1 here
            arg_error_text = "format asks for alignment not power of 2";
            to_align = 0;
        } else {
            const auto mask = static_cast<std::size_t>(align - 1);
            to_align = static_cast<int>((static_cast<std::size_t>(align) - (total & mask)) & mask);
        }
    }
    return opt;
}

void pack_int(std::string& b, std::uint64_t n, bool little, int size, bool negative) {
    std::array<char, max_int_size> buff{};
    buff[static_cast<std::size_t>(little ? 0 : size - 1)] = static_cast<char>(n & 0xFFU);
    for (int i = 1; i < size; ++i) {
        n >>= 8U;
        buff[static_cast<std::size_t>(little ? i : size - 1 - i)] = static_cast<char>(n & 0xFFU);
    }
    if (negative && size > size_lua_integer) {
        for (int i = size_lua_integer; i < size; ++i) {
            buff[static_cast<std::size_t>(little ? i : size - 1 - i)] = static_cast<char>(0xFF);
        }
    }
    b.append(buff.data(), static_cast<std::size_t>(size));
}

template <class T>
void pack_bytes(std::string& b, T value, bool little) {
    std::array<char, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    if (!little) {
        std::ranges::reverse(bytes);
    }
    b.append(bytes.data(), sizeof(T));
}

template <class T>
T unpack_bytes(const char* data, bool little) {
    std::array<char, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), data, sizeof(T));
    if (!little) {
        std::ranges::reverse(bytes);
    }
    T value{};
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

int str_pack(State& state) {
    std::string_view fmt;
    if (!check_view(state, 1, fmt)) {
        return c_function_error;
    }
    push(state, Value::nil()); // Lua's mark after the arguments: a missing one is nil
    Header h;
    std::string b;
    std::size_t total = 0;
    int arg_n = 1;
    std::size_t at = 0;
    while (at < fmt.size()) {
        int size = 0;
        int to_align = 0;
        std::string arg_text;
        const Option opt = get_details(h, total, fmt, at, size, to_align, arg_text);
        if (!h.error.empty()) {
            return raise_error(state, h.error);
        }
        if (!arg_text.empty()) {
            return arg_error(state, 1, arg_text);
        }
        total += static_cast<std::size_t>(to_align + size);
        b.append(static_cast<std::size_t>(to_align), '\0');
        ++arg_n;
        switch (opt) {
        case Option::Int: {
            std::int64_t n = 0;
            if (!check_integer(state, arg_n, n)) {
                return c_function_error;
            }
            if (size < size_lua_integer) {
                const auto lim = static_cast<std::int64_t>(
                    std::uint64_t{1} << static_cast<unsigned int>((size * 8) - 1));
                if (n < -lim || n >= lim) {
                    return arg_error(state, arg_n, "integer overflow");
                }
            }
            pack_int(b, static_cast<std::uint64_t>(n), h.little, size, n < 0);
            break;
        }
        case Option::Uint: {
            std::int64_t n = 0;
            if (!check_integer(state, arg_n, n)) {
                return c_function_error;
            }
            if (size < size_lua_integer &&
                static_cast<std::uint64_t>(n) >=
                    (std::uint64_t{1} << static_cast<unsigned>(size * 8))) {
                return arg_error(state, arg_n, "unsigned overflow");
            }
            pack_int(b, static_cast<std::uint64_t>(n), h.little, size, false);
            break;
        }
        case Option::Float:
        case Option::Number:
        case Option::Double: {
            double f = 0;
            if (!check_number(state, arg_n, f)) {
                return c_function_error;
            }
            if (opt == Option::Float) {
                pack_bytes(b, static_cast<float>(f), h.little);
            } else {
                pack_bytes(b, f, h.little);
            }
            break;
        }
        case Option::Char: {
            std::string_view s;
            if (!check_view(state, arg_n, s)) {
                return c_function_error;
            }
            if (s.size() > static_cast<std::size_t>(size)) {
                return arg_error(state, arg_n, "string longer than given size");
            }
            b += s;
            b.append(static_cast<std::size_t>(size) - s.size(), '\0');
            break;
        }
        case Option::String: {
            std::string_view s;
            if (!check_view(state, arg_n, s)) {
                return c_function_error;
            }
            if (std::cmp_less(size, sizeof(std::size_t)) &&
                s.size() >= (std::size_t{1} << static_cast<unsigned>(size * 8))) {
                return arg_error(state, arg_n, "string length does not fit in given size");
            }
            pack_int(b, s.size(), h.little, size, false);
            b += s;
            total += s.size();
            break;
        }
        case Option::Zstr: {
            std::string_view s;
            if (!check_view(state, arg_n, s)) {
                return c_function_error;
            }
            if (s.contains('\0')) {
                return arg_error(state, arg_n, "string contains zeros");
            }
            b += s;
            b += '\0';
            total += s.size() + 1;
            break;
        }
        case Option::Padding: b += '\0'; [[fallthrough]];
        case Option::PaddAlign:
        case Option::Nop: --arg_n; break;
        }
    }
    push_string(state, b);
    return 1;
}

int str_packsize(State& state) {
    std::string_view fmt;
    if (!check_view(state, 1, fmt)) {
        return c_function_error;
    }
    Header h;
    std::size_t total = 0;
    std::size_t at = 0;
    while (at < fmt.size()) {
        int size = 0;
        int to_align = 0;
        std::string arg_text;
        const Option opt = get_details(h, total, fmt, at, size, to_align, arg_text);
        if (!h.error.empty()) {
            return raise_error(state, h.error);
        }
        if (!arg_text.empty()) {
            return arg_error(state, 1, arg_text);
        }
        if (opt == Option::String || opt == Option::Zstr) {
            return arg_error(state, 1, "variable-length format");
        }
        size += to_align;
        if (total > max_size - static_cast<std::size_t>(size)) {
            return arg_error(state, 1, "format result too large");
        }
        total += static_cast<std::size_t>(size);
    }
    push(state, Value::integer(static_cast<std::int64_t>(total)));
    return 1;
}

std::optional<std::int64_t> unpack_int(State& state, const char* str, bool little, int size,
                                       bool is_signed) {
    std::uint64_t res = 0;
    const int limit = std::min(size, size_lua_integer);
    for (int i = limit - 1; i >= 0; --i) {
        res <<= 8U;
        res |= uchar(str[little ? i : size - 1 - i]);
    }
    if (size < size_lua_integer) {
        if (is_signed) {
            const std::uint64_t mask = std::uint64_t{1} << static_cast<unsigned>((size * 8) - 1);
            res = (res ^ mask) - mask;
        }
    } else if (size > size_lua_integer) {
        const int mask = !is_signed || static_cast<std::int64_t>(res) >= 0 ? 0 : 0xFF;
        for (int i = limit; i < size; ++i) {
            if (std::cmp_not_equal(uchar(str[little ? i : size - 1 - i]), mask)) {
                raise_error(state,
                            std::format("{}-byte integer does not fit into Lua Integer", size));
                return std::nullopt;
            }
        }
    }
    return static_cast<std::int64_t>(res);
}

int str_unpack(State& state) {
    std::string_view fmt;
    std::string_view data;
    std::int64_t init_arg = 0;
    if (!check_view(state, 1, fmt) || !check_view(state, 2, data) ||
        !opt_integer(state, 3, 1, init_arg)) {
        return c_function_error;
    }
    std::size_t pos = start_position(init_arg, data.size()) - 1;
    if (pos > data.size()) {
        return arg_error(state, 3, "initial position out of string");
    }
    Header h;
    int n = 0;
    std::size_t at = 0;
    while (at < fmt.size()) {
        int size = 0;
        int to_align = 0;
        std::string arg_text;
        const Option opt = get_details(h, pos, fmt, at, size, to_align, arg_text);
        if (!h.error.empty()) {
            return raise_error(state, h.error);
        }
        if (!arg_text.empty()) {
            return arg_error(state, 1, arg_text);
        }
        if (static_cast<std::size_t>(to_align) + static_cast<std::size_t>(size) >
            data.size() - pos) {
            return arg_error(state, 2, "data string too short");
        }
        pos += static_cast<std::size_t>(to_align);
        if (!state.check_stack(2)) {
            return raise_error(state, "too many results");
        }
        ++n;
        const char* p = data.data() + pos;
        switch (opt) {
        case Option::Int:
        case Option::Uint: {
            const auto r = unpack_int(state, p, h.little, size, opt == Option::Int);
            if (!r) {
                return c_function_error;
            }
            push(state, Value::integer(*r));
            break;
        }
        case Option::Float: push(state, Value::number(unpack_bytes<float>(p, h.little))); break;
        case Option::Number:
        case Option::Double: push(state, Value::number(unpack_bytes<double>(p, h.little))); break;
        case Option::Char:
            push_string(state, std::string_view{p, static_cast<std::size_t>(size)});
            break;
        case Option::String: {
            const auto r = unpack_int(state, p, h.little, size, false);
            if (!r) {
                return c_function_error;
            }
            const auto len = static_cast<std::size_t>(*r);
            if (len > data.size() - pos - static_cast<std::size_t>(size)) {
                return arg_error(state, 2, "data string too short");
            }
            push_string(state, std::string_view{p + size, len});
            pos += len;
            break;
        }
        case Option::Zstr: {
            const std::size_t len = std::strlen(p);
            if (pos + len >= data.size()) {
                return arg_error(state, 2, "unfinished string for format 'z'");
            }
            push_string(state, std::string_view{p, len});
            pos += len + 1;
            break;
        }
        case Option::PaddAlign:
        case Option::Padding:
        case Option::Nop: --n; break;
        }
        pos += static_cast<std::size_t>(size);
    }
    push(state, Value::integer(static_cast<std::int64_t>(pos) + 1));
    return n + 1;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array string_functions{
    LibraryFunction{.name = "byte", .function = &str_byte},
    LibraryFunction{.name = "char", .function = &str_char},
    LibraryFunction{.name = "dump", .function = &str_dump},
    LibraryFunction{.name = "find", .function = &str_find},
    LibraryFunction{.name = "format", .function = &str_format},
    LibraryFunction{.name = "gmatch", .function = &str_gmatch},
    LibraryFunction{.name = "gsub", .function = &str_gsub},
    LibraryFunction{.name = "len", .function = &str_len},
    LibraryFunction{.name = "lower", .function = &str_case<std::tolower>},
    LibraryFunction{.name = "match", .function = &str_match},
    LibraryFunction{.name = "rep", .function = &str_rep},
    LibraryFunction{.name = "reverse", .function = &str_reverse},
    LibraryFunction{.name = "sub", .function = &str_sub},
    LibraryFunction{.name = "upper", .function = &str_case<std::toupper>},
    LibraryFunction{.name = "pack", .function = &str_pack},
    LibraryFunction{.name = "packsize", .function = &str_packsize},
    LibraryFunction{.name = "unpack", .function = &str_unpack},
};

constexpr std::array string_metamethods{
    LibraryFunction{.name = "__add", .function = &string_arith<Event::Add>},
    LibraryFunction{.name = "__sub", .function = &string_arith<Event::Sub>},
    LibraryFunction{.name = "__mul", .function = &string_arith<Event::Mul>},
    LibraryFunction{.name = "__mod", .function = &string_arith<Event::Mod>},
    LibraryFunction{.name = "__pow", .function = &string_arith<Event::Pow>},
    LibraryFunction{.name = "__div", .function = &string_arith<Event::Div>},
    LibraryFunction{.name = "__idiv", .function = &string_arith<Event::IDiv>},
    LibraryFunction{.name = "__unm", .function = &string_arith<Event::Unm>},
};

} // namespace

void open_string(Heap& heap) {
    // luaopen_string and createmetatable: the strings' metatable has the
    // arithmetic metamethods, and __index is the library.
    const Table* library = open_library(heap, "string", string_functions);
    Table* mt = heap.new_table();
    set_functions(heap, *mt, string_metamethods);
    mt->set(heap, Value::string(heap.new_string("__index")), Value::table(library));
    heap.set_type_metatable(Type::String, mt);
}

} // namespace lua2
