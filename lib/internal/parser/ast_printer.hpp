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

// The same tree as a Graphviz DOT graph: one box per line of print_ast(),
// with an arrow from each node to its children, in source order:
//
//   digraph ast {
//     ...
//     n0 [label="Chunk"];
//     n1 [label="Return"];
//     n0 -> n1;
//     ...
//   }
//
// Used by `lua2 --ast=dot`; render it with `dot -Tsvg tree.dot -o tree.svg`.
std::string print_ast_dot(const Ast& ast);

} // namespace lua2
