#include <cpplua/source_file.hpp>

namespace cpplua {

std::string_view skip_file_prefix(std::string_view contents) {
    constexpr std::string_view bom = "\xEF\xBB\xBF";
    if (contents.starts_with(bom)) {
        contents.remove_prefix(bom.size());
    }
    if (contents.starts_with('#')) {
        // Lua skips up to the first '\n', and then gives the lexer a '\n' in
        // the line's place: here, the '\n' itself.
        const auto newline = contents.find('\n');
        contents.remove_prefix(newline == std::string_view::npos ? contents.size() : newline);
    }
    return contents;
}

} // namespace cpplua
