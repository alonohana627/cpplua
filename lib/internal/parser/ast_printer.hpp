#pragma once

#include <string>

#include "parser/ast.hpp"

namespace lua2 {

// Prints the tree as indented text, one node per line, two spaces per level:
//
//   Chunk
//     Return
//       Integer 1
//
// Used by `lua2 --ast` and by the parser tests, which compare this text.
std::string print_ast(const Ast& ast);

} // namespace lua2
