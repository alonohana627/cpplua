#include "parser/ast_printer.hpp"

namespace lua2 {

// NOLINTNEXTLINE(modernize-use-string-view): from part 1 on, the text is built.
std::string print_ast([[maybe_unused]] const Ast& ast) {
    // There are no statement nodes yet, so every chunk prints as just its
    // root. Part 1 prints each statement of ast.root() below it, indented one
    // level, with its children further in.
    return "Chunk\n";
}

} // namespace lua2
