#pragma once

#include <expected>
#include <string_view>

#include "gc/heap.hpp"
#include "parser/ast.hpp"
#include "syntax_error.hpp"
#include "vm/proto.hpp"

namespace cpplua {

// Compiles a parsed chunk to bytecode (docs/plan/vm.md): the prototype of
// its main function, with every function inside it.
//
// A port of Lua 5.4's code generator (lcode.c, and the half of lparser.c
// that drives it), walking the tree instead of the tokens, so it picks the
// same instructions, registers and constants as the real Lua: listings
// match `luac -l -l` (tests/vm_compiler_test.cpp).
//
// `source` is the chunk's name the way Lua keeps it ("@file.lua", "=stdin"),
// for runtime messages; `chunk_name` is how compile errors name the chunk.
// The protos are objects of `heap`. Nothing is collected while compiling,
// so nothing needs anchoring until the caller runs the result. Compiling
// fails only at Lua's limits: "function or expression needs too many
// registers", "control structure too long". `text` is the source code the
// Ast was parsed from, for the "near" part of those errors.
std::expected<Proto*, SyntaxError> compile(Heap& heap, const Ast& ast, std::string_view source,
                                           std::string_view chunk_name, std::string_view text = {});

} // namespace cpplua
