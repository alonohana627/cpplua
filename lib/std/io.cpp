// The io library: a port of Lua 5.4.9's liolib.c
// (docs/final_docs/stdlib.md), on the C++ standard library: a file is a
// userdata holding a std::filebuf (a stream buffer, for the standard input),
// where Lua's holds a C FILE*. What goes to the standard output and error
// goes through the heap's output functions, as print's does, so an
// embedding program (and the tests) can capture it.

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <span>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "gc/heap.hpp"
#include "std/auxlib.hpp"
#include "std/libs.hpp"
#include "value/convert.hpp"
#include "value/locale.hpp"
#include "value/number.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include "vm/execute.hpp"
#include "vm/function.hpp"
#include "vm/state.hpp"

namespace cpplua {

namespace {

// NOLINTBEGIN(misc-const-correctness,readability-non-const-parameter): the CFunction signature

using Traits = std::streambuf::traits_type;

// How a stream closes (luaL_Stream's closef): Closed marks a closed one.
enum class Closer : std::uint8_t { Closed, File, Standard };

// Which standard stream a stream is, if any.
enum class Standard : std::uint8_t { None, Input, Output, Error };

struct Stream {
    std::unique_ptr<std::filebuf> file; // a file cpplua opened
    std::streambuf* buffer = nullptr;   // where reads and writes go
    Standard standard = Standard::None;
    Closer closer = Closer::Closed;
    bool line_buffered = false; // setvbuf "line": flush at each newline
    bool directory = false;     // POSIX opens a directory; reading it fails (EISDIR)
    bool text = false;
    bool readable = false;                 // opened for reading, for writing
    bool writable = false;                 // Windows text mode: "\n" is "\r\n" in the file
    std::filesystem::path remove_on_close; // io.tmpfile's file
};

// The tag of file userdata, and the keys of the metatable ("FILE*") and of
// the default input and output files in the references table (Lua keeps
// them in the registry).
constexpr char file_tag = 0;
constexpr char input_key = 0;
constexpr char output_key = 0;
// Every file userdata, in a table with weak keys: what os.exit flushes.
constexpr char all_files_key = 0;

Value registry_get(Heap& heap, const void* key) {
    return heap.references().get(Value::light_userdata(key)).value();
}

void registry_set(Heap& heap, const void* key, Value v) {
    heap.references().set(heap, Value::light_userdata(key), v);
}

// luaL_testudata: the stream in v, if it is a file.
Stream* to_stream(Value v) {
    if (!v.is_userdata()) {
        return nullptr;
    }
    auto* u =
        const_cast<Userdata*>(v.as_userdata()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    return u->type_tag == &file_tag ? static_cast<Stream*>(u->data()) : nullptr;
}

// tolstream: argument n as a stream (luaL_checkudata).
Stream* check_stream(State& state, int n) {
    Stream* s = to_stream(arg(state, n));
    if (s == nullptr) {
        arg_type_error(state, n, "FILE*");
    }
    return s;
}

// tofile: argument n as an open stream.
Stream* check_open(State& state, int n) {
    Stream* s = check_stream(state, n);
    if (s != nullptr && s->closer == Closer::Closed) {
        raise_error(state, "attempt to use a closed file");
        return nullptr;
    }
    return s;
}

// A new file userdata (newprefile): closed until something opens it.
Stream* new_stream(Heap& heap, Value& out) {
    Userdata* u = heap.new_userdata(sizeof(Stream), 0);
    // The userdata owns the stream: its destructor runs when it is freed.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* s = new (u->data()) Stream{};
    u->type_tag = &file_tag;
    u->destructor = [](void* data) { static_cast<Stream*>(data)->~Stream(); };
    heap.set_metatable(u, table_object(registry_get(heap, &file_tag)));
    out = Value::userdata(u);
    table_object(registry_get(heap, &all_files_key))->set(heap, out, Value::boolean(true));
    return s;
}

Stream* new_stream(State& state) {
    Value v;
    Stream* s = new_stream(state.heap(), v);
    push(state, v);
    return s;
}

// fopen's modes as open modes: "r", "w", "a", with "+" for both ways and
// "b" for binary (l_checkmode has checked the string).
std::ios::openmode open_mode(std::string_view mode) {
    const bool update = mode.contains('+');
    std::ios::openmode m{};
    switch (mode.front()) {
    case 'r': m = update ? std::ios::in | std::ios::out : std::ios::in; break;
    case 'w':
        m = update ? std::ios::in | std::ios::out | std::ios::trunc
                   : std::ios::out | std::ios::trunc;
        break;
    default: m = update ? std::ios::in | std::ios::app : std::ios::app; break;
    }
    if (mode.contains('b')) {
        m |= std::ios::binary;
    }
    return m;
}

// Opens `name` into s; false with errno set when it cannot. Files open in
// binary mode; on Windows a text-mode file translates newlines itself
// (Reader, write_bytes).
bool open_file(Stream& s, const std::string& name, std::ios::openmode mode) {
    auto file = std::make_unique<std::filebuf>();
    errno = 0;
#ifdef _WIN32
    s.text = (mode & std::ios::binary) == 0;
#endif
    if (file->open(name, mode | std::ios::binary) == nullptr) {
        if (errno == 0) { // NOLINT(clang-analyzer-unix.Errno): set to 0 above, maybe by open
            std::error_code ec;
            errno = std::filesystem::exists(name, ec) ? EACCES : ENOENT;
        }
        return false;
    }
    s.buffer = file.get();
    s.file = std::move(file);
    std::error_code ec;
    s.directory = std::filesystem::is_directory(name, ec);
    s.readable = (mode & std::ios::in) != 0;
    s.writable = (mode & (std::ios::out | std::ios::app)) != 0;
    s.closer = Closer::File;
    return true;
}

// The close functions (io_fclose, io_noclose).
int close_stream(State& state, Stream& s, Closer closer) {
    switch (closer) {
    case Closer::File: {
        errno = 0;
        const bool ok = s.file->close() != nullptr;
        s.buffer = nullptr;
        if (!s.remove_on_close.empty()) {
            std::error_code ec;
            std::filesystem::remove(s.remove_on_close, ec);
            s.remove_on_close.clear();
        }
        return file_result(state, ok, nullptr);
    }
    case Closer::Standard:
        s.closer = Closer::Standard; // keep it open
        push(state, Value::nil());
        push_string(state, "cannot close standard file");
        return 2;
    case Closer::Closed: break;
    }
    return 0;
}

// aux_close
int aux_close(State& state, Stream& s) {
    const Closer closer = s.closer;
    s.closer = Closer::Closed;
    return close_stream(state, s, closer);
}

// ---- Writing ----

// Writes to s; the standard output and error go through the heap.
bool write_bytes(Heap& heap, Stream& s, std::string_view text) {
    switch (s.standard) {
    case Standard::Output: heap.write_output(text, false); return true;
    case Standard::Error: heap.write_error(text); return true;
    case Standard::Input: errno = EBADF; return false;
    case Standard::None: break;
    }
    if (!s.writable) { // the C library's EBADF
        errno = EBADF;
        return false;
    }
    std::string translated;
    if (s.text && text.contains('\n')) { // "\n" is "\r\n" in a text file
        for (const char c : text) {
            if (c == '\n') {
                translated += '\r';
            }
            translated += c;
        }
        text = translated;
    }
    const auto n = static_cast<std::streamsize>(text.size());
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): writes n bytes
    if (s.buffer->sputn(text.data(), n) != n) {
        errno = EIO;
        return false;
    }
    if (s.line_buffered && text.contains('\n')) {
        s.buffer->pubsync();
    }
    return true;
}

// g_write: the arguments from `first` on.
int write_values(State& state, Stream& s, int first) {
    const int n = arg_count(state);
    bool ok = true;
    errno = 0;
    for (int i = first; i <= n; ++i) {
        const Value v = arg(state, i);
        if (v.is_integer()) {
            ok = write_bytes(state.heap(), s, std::to_string(v.as_integer())) && ok;
        } else if (v.is_float()) {
            // LUA_NUMBER_FMT
            ok = write_bytes(state.heap(), s, c_format_number("%.14g", v.as_float())) && ok;
        } else {
            std::string_view text;
            if (!check_view(state, i, text)) {
                return c_function_error;
            }
            ok = write_bytes(state.heap(), s, text) && ok;
        }
    }
    return ok ? 1 : file_result(state, false, nullptr);
}

// ---- Reading ----

// The buffer reads come from: none for the output streams.
std::streambuf* read_buffer(const Stream& s) {
    return s.standard == Standard::Output || s.standard == Standard::Error ? nullptr : s.buffer;
}

// Reads a stream's characters (getc, ungetc). A file opened in text mode
// on Windows reads "\r\n" as "\n", as the C library's text files do there
// (cpplua opens every file in binary mode, so positions are the file's
// bytes, as ftell gives them).
class Reader {
public:
    Reader(std::streambuf& buffer, bool text) : buffer_(buffer), text_(text) {}

    // The next character, not taken.
    int peek() {
        const int c = buffer_.sgetc();
        if (!text_ || c != '\r') {
            return c;
        }
        buffer_.sbumpc();
        const int next = buffer_.sgetc();
        buffer_.sungetc();
        return next == '\n' ? '\n' : c;
    }

    // The next character, taken.
    int get() {
        const int c = buffer_.sbumpc();
        if (text_ && c == '\r' && buffer_.sgetc() == '\n') {
            return buffer_.sbumpc();
        }
        return c;
    }

    // Up to n characters.
    std::string read(std::size_t n) {
        std::string out;
        if (!text_) {
            out.resize(n);
            const std::streamsize got = buffer_.sgetn(out.data(), static_cast<std::streamsize>(n));
            out.resize(static_cast<std::size_t>(got));
            return out;
        }
        for (int c = 0; out.size() < n && (c = get()) != Traits::eof();) {
            out += static_cast<char>(c);
        }
        return out;
    }

    std::string read_all() {
        std::string out;
        std::array<char, 4096> chunk{};
        std::streamsize got = 0;
        if (!text_) {
            do {
                got = buffer_.sgetn(chunk.data(), chunk.size());
                out.append(chunk.data(), static_cast<std::size_t>(got));
            } while (std::cmp_equal(got, chunk.size()));
            return out;
        }
        for (int c = 0; (c = get()) != Traits::eof();) {
            out += static_cast<char>(c);
        }
        return out;
    }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): lives only during a read
    std::streambuf& buffer_;
    bool text_;
};

bool is_space(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

bool is_digit(int c, bool hex) {
    return (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
}

// RN and read_number: the longest prefix that may be a numeral (up to 200
// characters), converted as tonumber does. `c` is the next character, not
// yet taken, where Lua reads one ahead and puts it back.
constexpr std::size_t max_number_length = 200; // L_MAXLENNUM

struct NumberReader {
    Reader* reader;
    int c = Traits::eof();
    std::string buff;
    bool overflow = false;

    bool next() {
        if (buff.size() >= max_number_length) {
            overflow = true;
            return false;
        }
        buff += static_cast<char>(c);
        reader->get();
        c = reader->peek();
        return true;
    }

    bool test2(std::string_view set) { return (c == set[0] || c == set[1]) && next(); }

    int digits(bool hex) {
        int count = 0;
        while (is_digit(c, hex) && next()) {
            ++count;
        }
        return count;
    }
};

bool read_number(State& state, Reader& reader) {
    while (is_space(reader.peek())) {
        reader.get();
    }
    NumberReader rn{.reader = &reader, .c = reader.peek(), .buff = {}, .overflow = false};
    int count = 0;
    bool hex = false;
    rn.test2("-+");
    if (rn.test2("00")) {
        if (rn.test2("xX")) {
            hex = true;
        } else {
            count = 1; // the initial '0' is a digit
        }
    }
    count += rn.digits(hex);
    const char point = locale_decimal_point();
    if (rn.test2(std::string{point, '.'})) { // the locale's decimal point, or a dot
        count += rn.digits(hex);
    }
    if (count > 0 && rn.test2(hex ? "pP" : "eE")) {
        rn.test2("-+");
        rn.digits(false);
    }
    if (!rn.overflow) {
        if (const auto n = string_to_number(rn.buff)) {
            push(state, *n);
            return true;
        }
    }
    push(state, Value::nil());
    return false;
}

bool test_eof(State& state, Reader& reader) {
    push_string(state, "");
    return reader.peek() != Traits::eof();
}

bool read_line(State& state, Reader& reader, bool chop) {
    std::string line;
    int c = Traits::eof();
    while ((c = reader.get()) != Traits::eof() && c != '\n') {
        line += static_cast<char>(c);
    }
    if (!chop && c == '\n') {
        line += '\n';
    }
    push_string(state, line);
    return c == '\n' || !line.empty();
}

// g_read: a value per format, the arguments from `first` on.
int read_values(State& state, Stream& s, int first) {
    std::streambuf* buffer = read_buffer(s);
    if (buffer == nullptr || (s.standard == Standard::None && !s.readable)) {
        // reading an output stream: the C library's EBADF
        return error_result(state, EBADF, nullptr);
    }
    if (s.directory) {
        return error_result(state, EISDIR, nullptr);
    }
    Reader reader{*buffer, s.text};
    const int nargs = arg_count(state) - first + 1;
    errno = 0;
    bool success = true;
    int n = first;
    if (nargs <= 0) {
        success = read_line(state, reader, true);
        n = first + 1;
    } else {
        if (!state.check_stack(static_cast<std::uint32_t>(nargs) + 20)) {
            return raise_error(state, "stack overflow (too many arguments)");
        }
        for (int left = nargs; left > 0 && success; --left, ++n) {
            const Value format = arg(state, n);
            if (format.is_number()) {
                std::int64_t l = 0;
                if (!check_integer(state, n, l)) {
                    return c_function_error;
                }
                if (l == 0) {
                    success = test_eof(state, reader);
                } else {
                    const std::string text = reader.read(static_cast<std::size_t>(l));
                    push_string(state, text);
                    success = !text.empty();
                }
                continue;
            }
            std::string_view p;
            if (!check_view(state, n, p)) {
                return c_function_error;
            }
            if (p.starts_with('*')) {
                p.remove_prefix(1); // the optional '*' of Lua 5.2
            }
            switch (p.empty() ? '\0' : p.front()) {
            case 'n': success = read_number(state, reader); break;
            case 'l': success = read_line(state, reader, true); break;
            case 'L': success = read_line(state, reader, false); break;
            case 'a':
                push_string(state, reader.read_all());
                success = true;
                break;
            default: return arg_error(state, n, "invalid format");
            }
        }
    }
    if (!success) {
        state.set(state.top - 1, Value::nil()); // fail for the last result
    }
    return n - first;
}

// ---- The default files ----

// getiofile: pushes the default file and gives its stream.
Stream* default_file(State& state, const void* key, std::string_view name) {
    const Value v = registry_get(state.heap(), key);
    push(state, v);
    Stream* s = to_stream(v);
    if (s == nullptr || s->closer == Closer::Closed) {
        raise_error(state, std::format("default {} file is closed", name));
        return nullptr;
    }
    return s;
}

// opencheck
bool open_checked(State& state, const std::string& name, std::string_view mode) {
    Stream* s = new_stream(state);
    if (!open_file(*s, name, open_mode(mode))) {
        raise_error(state, std::format("cannot open file '{}' ({})", name,
                                       std::generic_category().message(errno)));
        return false;
    }
    return true;
}

// g_iofile: io.input and io.output.
int default_file_function(State& state, const void* key, std::string_view mode) {
    if (!arg_is_none_or_nil(state, 1)) {
        const Value v = arg(state, 1);
        if (v.is_string() || v.is_number()) {
            std::string_view name;
            if (!check_view(state, 1, name) || !open_checked(state, std::string{name}, mode)) {
                return c_function_error;
            }
            registry_set(state.heap(), key, state.get(state.top - 1));
        } else {
            if (check_open(state, 1) == nullptr) {
                return c_function_error;
            }
            registry_set(state.heap(), key, v);
        }
    }
    push(state, registry_get(state.heap(), key));
    return 1;
}

// Flushes s (fflush): the standard output through the heap.
bool flush_stream(Heap& heap, Stream& s) {
    switch (s.standard) {
    case Standard::Output: heap.flush_output(); return true;
    case Standard::Error: std::cerr.flush(); return true;
    case Standard::Input: return true;
    case Standard::None: break;
    }
    return s.buffer->pubsync() == 0;
}

// ---- The io functions ----

int io_close(State& state) {
    if (arg_is_none(state, 1)) {
        push(state, registry_get(state.heap(), &output_key)); // the default output
    }
    Stream* s = check_open(state, 1);
    return s == nullptr ? c_function_error : aux_close(state, *s);
}

int io_flush(State& state) {
    Stream* s = default_file(state, &output_key, "output");
    if (s == nullptr) {
        return c_function_error;
    }
    errno = 0;
    return file_result(state, flush_stream(state.heap(), *s), nullptr);
}

int io_input(State& state) {
    return default_file_function(state, &input_key, "r");
}

int io_output(State& state) {
    return default_file_function(state, &output_key, "w");
}

// l_checkmode: [rwa]%+?b*
bool valid_mode(std::string_view mode) {
    if (mode.empty() || !std::string_view{"rwa"}.contains(mode.front())) {
        return false;
    }
    mode.remove_prefix(1);
    if (mode.starts_with('+')) {
        mode.remove_prefix(1);
    }
    return mode.find_first_not_of('b') == std::string_view::npos;
}

int io_open(State& state) {
    std::string_view name;
    std::string_view mode;
    if (!check_view(state, 1, name) || !opt_view(state, 2, "r", mode)) {
        return c_function_error;
    }
    Stream* s = new_stream(state);
    if (!valid_mode(mode)) {
        return arg_error(state, 2, "invalid mode");
    }
    const std::string file_name{name};
    if (!open_file(*s, file_name, open_mode(mode))) {
        return file_result(state, false, file_name.c_str());
    }
    return 1;
}

// io.popen: the C++ standard library cannot run a process with a pipe, so
// this is what a Lua without popen says (l_popen's fallback).
int io_popen(State& state) {
    std::string_view command;
    std::string_view mode;
    if (!check_view(state, 1, command) || !opt_view(state, 2, "r", mode)) {
        return c_function_error;
    }
    new_stream(state);
    if (mode != "r" && mode != "w") { // l_checkmodep
        return arg_error(state, 2, "invalid mode");
    }
    return raise_error(state, "'popen' not supported");
}

int io_read(State& state) {
    Stream* s = default_file(state, &input_key, "input");
    if (s == nullptr) {
        return c_function_error;
    }
    state.top -= 1; // the file default_file pushed
    return read_values(state, *s, 1);
}

// io.tmpfile: a new file in the temporary folder, opened for update and
// removed when closed.
int io_tmpfile(State& state) {
    Stream* s = new_stream(state);
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return error_result(state, ec.default_error_condition().value(), nullptr);
    }
    static unsigned int counter = 0;
    const auto stamp = std::bit_cast<std::uintptr_t>(s);
    constexpr auto mode = std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto path = folder / std::format("cpplua_{:x}_{:x}.tmp", stamp, ++counter);
        if (std::filesystem::exists(path, ec)) {
            continue;
        }
        if (!open_file(*s, path.string(), mode)) {
            return file_result(state, false, nullptr);
        }
        s->remove_on_close = path;
        return 1;
    }
    return error_result(state, EEXIST, nullptr);
}

int io_type(State& state) {
    if (!check_any(state, 1)) {
        return c_function_error;
    }
    const Stream* s = to_stream(arg(state, 1));
    if (s == nullptr) {
        push(state, Value::nil());
    } else {
        push_string(state, s->closer == Closer::Closed ? "closed file" : "file");
    }
    return 1;
}

int io_write(State& state) {
    Stream* s = default_file(state, &output_key, "output");
    if (s == nullptr) {
        return c_function_error;
    }
    // The file is on top, past the arguments: write the ones before it.
    const Value file = state.get(state.top - 1);
    state.top -= 1;
    const int r = write_values(state, *s, 1);
    if (r == 1) {
        push(state, file);
    }
    return r;
}

// io_readline: the iterator of lines(); upvalues are the file, the number
// of formats, whether to close the file at the end, and the formats.
constexpr int max_line_arguments = 250; // MAXARGLINE

int lines_step(State& state) {
    const auto* closure = state.get(state.ci->func).as_c_closure();
    const auto upvalues = closure->upvalues();
    // debug.setupvalue can replace any upvalue, so none of them is trusted.
    Stream* s = to_stream(upvalues[0]);
    if (s == nullptr) {
        return raise_error(state, "bad file in 'lines' iterator");
    }
    if (s->closer == Closer::Closed) {
        return raise_error(state, "file is already closed");
    }
    // lua_tointeger: 0 when it is not an integer; the formats past the
    // closure's upvalues are nil, as lua_upvalueindex past the count reads.
    const auto count = to_integer(upvalues[1]).value_or(0);
    const auto n = static_cast<int>(std::clamp<std::int64_t>(count, 0, max_line_arguments));
    state.top = arg_slot(state, 1) + 1; // lua_settop(L, 1)
    if (!state.check_stack(static_cast<std::uint32_t>(n) + 20)) {
        return raise_error(state, "stack overflow (too many arguments)");
    }
    for (std::size_t i = 0; std::cmp_less(i, n); ++i) {
        state.push(3 + i < upvalues.size() ? upvalues[3 + i] : Value::nil());
    }
    const int results = read_values(state, *s, 2);
    if (results == c_function_error) {
        return c_function_error;
    }
    if (state.get(state.top - static_cast<std::uint32_t>(results)).is_truthy()) {
        return results;
    }
    if (results > 1) { // an error message
        const Value message = state.get(state.top - static_cast<std::uint32_t>(results) + 1);
        return raise_error(state, message.is_string() ? message.as_string()->view() : "?");
    }
    if (upvalues[2].is_truthy()) { // lines() opened the file: close it
        const std::uint32_t top = state.top;
        aux_close(state, *s);
        state.top = top;
    }
    return 0;
}

// aux_lines: the iterator over the file at argument 1, with the formats
// after it.
int push_lines(State& state, bool to_close) {
    const int n = arg_count(state) - 1;
    if (n > max_line_arguments) {
        return arg_error(state, max_line_arguments + 2, "too many arguments");
    }
    CClosure* closure = state.heap().new_c_closure(&lines_step, 3 + static_cast<std::size_t>(n));
    const auto upvalues = closure->upvalues();
    upvalues[0] = arg(state, 1);
    upvalues[1] = Value::integer(n);
    upvalues[2] = Value::boolean(to_close);
    for (int i = 0; i < n; ++i) {
        upvalues[3 + static_cast<std::size_t>(i)] = arg(state, 2 + i);
    }
    push(state, Value::c_closure(closure));
    return 1;
}

int io_lines(State& state) {
    if (arg_is_none(state, 1)) {
        push(state, Value::nil());
    }
    bool to_close = false;
    if (arg(state, 1).is_nil()) { // the default input
        state.set(arg_slot(state, 1), registry_get(state.heap(), &input_key));
        if (check_open(state, 1) == nullptr) {
            return c_function_error;
        }
    } else {
        std::string_view name;
        if (!check_view(state, 1, name) || !open_checked(state, std::string{name}, "r")) {
            return c_function_error;
        }
        state.set(arg_slot(state, 1), state.get(state.top - 1));
        state.top -= 1;
        to_close = true;
    }
    if (push_lines(state, to_close) == c_function_error) {
        return c_function_error;
    }
    if (to_close) {
        push(state, Value::nil());  // state
        push(state, Value::nil());  // control
        push(state, arg(state, 1)); // the file, the to-be-closed variable
        return 4;
    }
    return 1;
}

// ---- File methods ----

int f_close(State& state) {
    Stream* s = check_open(state, 1);
    return s == nullptr ? c_function_error : aux_close(state, *s);
}

int f_flush(State& state) {
    Stream* s = check_open(state, 1);
    if (s == nullptr) {
        return c_function_error;
    }
    errno = 0;
    return file_result(state, flush_stream(state.heap(), *s), nullptr);
}

int f_lines(State& state) {
    if (check_open(state, 1) == nullptr) {
        return c_function_error;
    }
    return push_lines(state, false);
}

int f_read(State& state) {
    Stream* s = check_open(state, 1);
    return s == nullptr ? c_function_error : read_values(state, *s, 2);
}

int f_seek(State& state) {
    Stream* s = check_open(state, 1);
    if (s == nullptr) {
        return c_function_error;
    }
    static constexpr std::array<std::string_view, 3> names{"set", "cur", "end"};
    static constexpr std::array<std::ios::seekdir, 3> directions{
        std::ios::beg,
        std::ios::cur,
        std::ios::end,
    };
    std::size_t op = 0;
    std::int64_t offset = 0;
    if (!check_option(state, 2, "cur", names, op) || !opt_integer(state, 3, 0, offset)) {
        return c_function_error;
    }
    if (s->standard != Standard::None) {
        // The standard streams go through the C library's (std::cout and
        // friends are synchronized with stdio): seek there, as Lua does, so
        // a redirected stdin can seek. Output an embedder captures has no
        // position.
        Heap& heap = state.heap();
        const bool captured = (s->standard == Standard::Output && heap.captures_output()) ||
                              (s->standard == Standard::Error && heap.captures_error_output());
        if (captured) {
            return error_result(state, ESPIPE, nullptr);
        }
        std::FILE* c_file = stderr;
        if (s->standard == Standard::Input) {
            c_file = stdin;
        } else if (s->standard == Standard::Output) {
            c_file = stdout;
        }
        static constexpr std::array<int, 3> whence{SEEK_SET, SEEK_CUR, SEEK_END};
        std::cout.flush();
        errno = 0;
#ifdef _WIN32
        const bool failed = _fseeki64(c_file, offset, whence.at(op)) != 0;
        const std::int64_t position = failed ? -1 : _ftelli64(c_file);
#else
        const bool failed = fseeko(c_file, static_cast<off_t>(offset), whence.at(op)) != 0;
        const std::int64_t position = failed ? -1 : ftello(c_file);
#endif
        if (failed || position < 0) {
            return error_result(state, errno != 0 ? errno : ESPIPE, nullptr);
        }
        push(state, Value::integer(position));
        return 1;
    }
    const auto at = s->buffer->pubseekoff(static_cast<std::streamoff>(offset), directions.at(op),
                                          std::ios::in | std::ios::out);
    if (at == std::streampos{std::streamoff{-1}}) {
        return error_result(state, EINVAL, nullptr);
    }
    push(state, Value::integer(static_cast<std::int64_t>(static_cast<std::streamoff>(at))));
    return 1;
}

int f_setvbuf(State& state) {
    Stream* s = check_open(state, 1);
    if (s == nullptr) {
        return c_function_error;
    }
    static constexpr std::array<std::string_view, 3> names{"no", "full", "line"};
    std::size_t op = 0;
    std::int64_t size = 0;
    if (!check_option(state, 2, "", names, op) || !opt_integer(state, 3, 1024, size)) {
        return c_function_error;
    }
    // "no": every write reaches the file at once; "full" and "line" buffer
    // again ("line" flushing at each newline). A filebuf drops what its
    // buffer holds when it changes buffers, so what waits to be written is
    // written first, and the position (past what was read ahead) comes back.
    if (s->standard == Standard::None) {
        std::streambuf& buffer = *s->buffer;
        const auto position = buffer.pubseekoff(0, std::ios::cur, std::ios::in | std::ios::out);
        buffer.pubsync();
        const auto bytes = size > 0 ? static_cast<std::streamsize>(size) : 1024;
        buffer.pubsetbuf(nullptr, op == 0 ? 0 : bytes);
        if (position != std::streampos{std::streamoff{-1}}) {
            buffer.pubseekpos(position, std::ios::in | std::ios::out);
        }
    }
    s->line_buffered = op == 2;
    push(state, Value::boolean(true));
    return 1;
}

int f_write(State& state) {
    Stream* s = check_open(state, 1);
    if (s == nullptr) {
        return c_function_error;
    }
    const int r = write_values(state, *s, 2);
    if (r == 1) {
        push(state, arg(state, 1));
    }
    return r;
}

int f_gc(State& state) {
    Stream* s = check_stream(state, 1);
    if (s == nullptr) {
        return c_function_error;
    }
    if (s->closer != Closer::Closed && s->buffer != nullptr) {
        aux_close(state, *s); // a failure is ignored
    }
    return 0;
}

int f_tostring(State& state) {
    const Stream* s = check_stream(state, 1);
    if (s == nullptr) {
        return c_function_error;
    }
    if (s->closer == Closer::Closed) {
        push_string(state, "file (closed)");
    } else {
        const auto address = std::bit_cast<std::uintptr_t>(s->buffer);
#ifdef _WIN32
        push_string(state, std::format("file ({:016x})", address));
#else
        push_string(state, std::format("file (0x{:x})", address));
#endif
    }
    return 1;
}

// NOLINTEND(misc-const-correctness,readability-non-const-parameter)

constexpr std::array io_functions{
    LibraryFunction{.name = "close", .function = &io_close},
    LibraryFunction{.name = "flush", .function = &io_flush},
    LibraryFunction{.name = "input", .function = &io_input},
    LibraryFunction{.name = "lines", .function = &io_lines},
    LibraryFunction{.name = "open", .function = &io_open},
    LibraryFunction{.name = "output", .function = &io_output},
    LibraryFunction{.name = "popen", .function = &io_popen},
    LibraryFunction{.name = "read", .function = &io_read},
    LibraryFunction{.name = "tmpfile", .function = &io_tmpfile},
    LibraryFunction{.name = "type", .function = &io_type},
    LibraryFunction{.name = "write", .function = &io_write},
};

constexpr std::array file_methods{
    LibraryFunction{.name = "read", .function = &f_read},
    LibraryFunction{.name = "write", .function = &f_write},
    LibraryFunction{.name = "lines", .function = &f_lines},
    LibraryFunction{.name = "flush", .function = &f_flush},
    LibraryFunction{.name = "seek", .function = &f_seek},
    LibraryFunction{.name = "close", .function = &f_close},
    LibraryFunction{.name = "setvbuf", .function = &f_setvbuf},
};

constexpr std::array file_metamethods{
    LibraryFunction{.name = "__gc", .function = &f_gc},
    LibraryFunction{.name = "__close", .function = &f_gc},
    LibraryFunction{.name = "__tostring", .function = &f_tostring},
};

// createstdfile
void standard_file(Heap& heap, Table& library, Standard which, std::streambuf* buffer,
                   const void* key, std::string_view name) {
    Value v;
    Stream* s = new_stream(heap, v);
    s->buffer = buffer;
    s->standard = which;
    s->closer = Closer::Standard;
    if (key != nullptr) {
        registry_set(heap, key, v);
    }
    library.set(heap, Value::string(heap.new_string(name)), v);
}

} // namespace

void flush_all_files(Heap& heap) {
    // What the C library's exit() does with its FILE*s: flush every open
    // file, and delete io.tmpfile's files.
    const Value all = registry_get(heap, &all_files_key);
    if (!all.is_table()) {
        return;
    }
    Value key;
    Value value;
    while (table_object(all)->next_entry(key, value) == Table::NextResult::Found) {
        Stream* s = to_stream(key);
        if (s == nullptr || s->closer == Closer::Closed || s->buffer == nullptr) {
            continue;
        }
        s->buffer->pubsync();
        if (!s->remove_on_close.empty() && s->file != nullptr) {
            s->file->close();
            std::error_code ec;
            std::filesystem::remove(s->remove_on_close, ec);
        }
    }
}

void open_io(Heap& heap) {
    Table* library = open_library(heap, "io", io_functions);
    Table* all_files = heap.new_table();
    Table* weak_keys = heap.new_table();
    weak_keys->set(heap, Value::string(heap.new_string("__mode")),
                   Value::string(heap.new_string("k")));
    heap.set_metatable(all_files, weak_keys);
    registry_set(heap, &all_files_key, Value::table(all_files));
    // createmeta: the metatable of files, named "FILE*", with the methods
    // as its __index.
    Table* mt = heap.new_table();
    registry_set(heap, &file_tag, Value::table(mt));
    set_functions(heap, *mt, file_metamethods);
    Table* methods = heap.new_table();
    set_functions(heap, *methods, file_methods);
    mt->set(heap, Value::string(heap.new_string("__index")), Value::table(methods));
    mt->set(heap, Value::string(heap.new_string("__name")),
            Value::string(heap.new_string("FILE*")));
    standard_file(heap, *library, Standard::Input, std::cin.rdbuf(), &input_key, "stdin");
    standard_file(heap, *library, Standard::Output, std::cout.rdbuf(), &output_key, "stdout");
    standard_file(heap, *library, Standard::Error, std::cerr.rdbuf(), nullptr, "stderr");
}

} // namespace cpplua
