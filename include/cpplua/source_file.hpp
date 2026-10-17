#pragma once

#include <string_view>

namespace cpplua {

// What Lua's file loader (luaL_loadfilex in lauxlib.c, `skipcomment`) drops
// from the start of a file before the lexer sees it:
//
// - a UTF-8 byte order mark, EF BB BF;
// - then, if the file starts with '#', its whole first line, so a Unix
//   "#!/usr/bin/env lua" line works. The '\n' that ends it is kept, so line
//   numbers in error messages stay right.
//
// Only files get this: a chunk given as a string (`load`) keeps every byte,
// and a '#' at its start is a syntax error, as in Lua.
//
// Returns the part of `contents` to lex, a view into it.
std::string_view skip_file_prefix(std::string_view contents);

} // namespace cpplua
