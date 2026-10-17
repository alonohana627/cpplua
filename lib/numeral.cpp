#include "numeral.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <system_error>

namespace cpplua {

namespace {

constexpr bool has_hex_prefix(std::string_view text) {
    return text.starts_with("0x") || text.starts_with("0X");
}

} // namespace

std::optional<std::int64_t> integer_numeral(std::string_view text, bool negative) {
    std::uint64_t magnitude = 0;
    if (has_hex_prefix(text)) {
        // Hex: at least one hex digit, and nothing else (L5.5). The value
        // wraps around modulo 2^64: unsigned arithmetic wraps by definition.
        const std::string_view digits = text.substr(2);
        if (digits.empty() || !std::ranges::all_of(digits, is_hex_digit)) {
            return std::nullopt;
        }
        for (const char c : digits) {
            magnitude = (magnitude * 16) + hex_value(c);
        }
    } else {
        // Decimal: from_chars must use the whole text (it accepts no sign
        // for an unsigned number), and must not overflow 64 bits.
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), magnitude);
        if (ec != std::errc{} || end != text.data() + text.size()) {
            return std::nullopt;
        }
        // The largest magnitude is 2^63 - 1, or 2^63 after a minus sign.
        constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (magnitude > max + (negative ? 1 : 0)) {
            return std::nullopt;
        }
    }
    // 0 - magnitude wraps, so -2^63 and negative hex come out right.
    return static_cast<std::int64_t>(negative ? 0 - magnitude : magnitude);
}

std::optional<double> float_numeral(std::string_view text) {
    // from_chars accepts a leading '-', and inf, nan and infinity (all with
    // an 'n'); a numeral has none of them.
    if (text.starts_with('-') || text.find_first_of("nN") != std::string_view::npos) {
        return std::nullopt;
    }
    // Hex floats use from_chars' hex format, which expects the digits
    // without the 0x prefix.
    const bool hex = has_hex_prefix(text);
    const std::string_view digits = hex ? text.substr(2) : text;
    if (hex && digits.starts_with('-')) {
        return std::nullopt;
    }
    const auto format = hex ? std::chars_format::hex : std::chars_format::general;

    // from_chars reports values out of range as errc::result_out_of_range,
    // and libc++ still stores inf or 0 in `value` (the lexer tests pin this
    // down), so that code is accepted too.
    double value = 0.0;
    const auto [end, ec] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value, format);
    if ((ec != std::errc{} && ec != std::errc::result_out_of_range) ||
        end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace cpplua
