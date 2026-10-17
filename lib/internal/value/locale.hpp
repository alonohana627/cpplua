#pragma once

namespace lua2 {

// What the C library's locale changes in Lua's values (P4 in
// docs/spec/conformance.md): string order (LC_COLLATE, through strcoll) and
// the decimal point numerals use (LC_NUMERIC). Programs start in the "C" locale; os.setlocale
// changes it, then calls refresh_locale. Kept in atomics: the locale belongs to the whole program.

// Reads the locale again (after std::setlocale).
void refresh_locale();

// Whether strings order byte by byte (LC_COLLATE is "C" or "POSIX").
[[nodiscard]] bool collation_is_bytes();

// Whether character classes are the "C" locale's (LC_CTYPE): pattern
// classes such as %a then need not ask the C library.
[[nodiscard]] bool ctype_is_c();

// The locale's decimal point (lua_getlocaledecpoint): '.' in "C".
[[nodiscard]] char locale_decimal_point();

} // namespace lua2
