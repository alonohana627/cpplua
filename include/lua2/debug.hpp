#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace lua2 {

// Debugging views of what the interpreter sees. They are public so the
// `lua2` program (and embedders) can show them without seeing internals.

// Lexes `source` and lists every token, one per line:
//
//   <line> <kind> [<value>]
//   1      Local
//   1      Name    x
//   1      String  "a\n"
//
// Returns the lexer's error message instead if lexing fails, such as
// `test.lua:3: unfinished string near '"abc'`.
std::expected<std::string, std::string> token_dump(std::string_view source,
                                                   std::string_view chunk_name = "?");

// Parses `source` and prints its syntax tree as indented text, one node per
// line (see `lua2 --ast`). Returns the error message instead if lexing or
// parsing fails, such as `test.lua:1: <eof> expected near 'x'`.
std::expected<std::string, std::string> ast_dump(std::string_view source,
                                                 std::string_view chunk_name = "?");

// Like ast_dump, but as a Graphviz DOT graph (see `lua2 --ast=dot`), to draw
// the tree with `dot -Tsvg tree.dot -o tree.svg`.
std::expected<std::string, std::string> ast_dot_dump(std::string_view source,
                                                     std::string_view chunk_name = "?");

// Parses and compiles `source` and lists its bytecode in the style of
// `luac -l -l` (see `lua2 --bytecode`): every function's instructions, with
// their source lines, constants, locals and upvalues. Returns the error
// message instead if the source does not compile.
std::expected<std::string, std::string> bytecode_dump(std::string_view source,
                                                      std::string_view chunk_name = "?");

} // namespace lua2
