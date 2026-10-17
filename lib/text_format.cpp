#include "text_format.hpp"

#include <format>

namespace cpplua {

std::string quoted(std::string_view bytes) {
    std::string out = "\"";
    for (const char c : bytes) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c >= ' ' && c <= '~') {
                out += c;
            } else {
                out += std::format("\\x{:02X}", static_cast<unsigned char>(c));
            }
            break;
        }
    }
    out += '"';
    return out;
}

std::string format_float(double value) {
    // std::format("{}") gives the shortest text that reads back exactly.
    // Like Lua, add ".0" when it would look like an integer, so 1e3 shows as
    // 1000.0, not 1000. (Text with '.', 'e', "inf" or "nan" is left alone.)
    std::string text = std::format("{}", value);
    if (text.find_first_of(".eni") == std::string::npos) {
        text += ".0";
    }
    return text;
}

} // namespace cpplua
