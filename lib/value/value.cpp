#include "value/value.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <format>

#include "text_format.hpp"
#include "value/string.hpp"

namespace lua2 {

void Value::wrong_access(std::string_view wanted, std::source_location caller) const {
    // Only C stdio writes of plain text: this runs when something is already
    // wrong, so it must not allocate or throw.
    const auto write = [](std::string_view text) {
        std::fwrite(text.data(), 1, text.size(), stderr);
    };
    std::array<char, 16> line{};
    const auto [end, error] = std::to_chars(line.data(), line.data() + line.size(), caller.line());
    write("lua2 internal error: ");
    write(caller.file_name());
    write(":");
    write(std::string_view{line.data(), end});
    write(": asked a ");
    write(tag_name(tag_));
    write(" value for its ");
    write(wanted);
    write("\n");
    std::abort();
}

std::string_view tag_name(Tag tag) {
    switch (tag) {
    case Tag::Nil: return "nil";
    case Tag::False: return "false";
    case Tag::True: return "true";
    case Tag::Integer: return "integer";
    case Tag::Float: return "float";
    case Tag::ShortString: return "short string";
    case Tag::LongString: return "long string";
    }
    return "?"; // not reached: every Tag is handled above
}

std::string debug_text(Value value) {
    switch (value.tag()) {
    case Tag::Nil: return "nil";
    case Tag::False: return "false";
    case Tag::True: return "true";
    case Tag::Integer: return std::format("{}", value.as_integer());
    case Tag::Float: return format_float(value.as_float());
    case Tag::ShortString:
    case Tag::LongString: return quoted(value.as_string()->view());
    }
    return "?"; // not reached: every Tag is handled above
}

} // namespace lua2
