#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "parser/ast.hpp"

namespace cpplua {

// Tracks what is visible where while the parser runs: the functions being
// parsed, their blocks, their local variables, labels and pending gotos.
// The parser calls it as it enters and leaves each construct, exactly where
// Lua's lparser.c calls open_func, enterblock, new_localvar, adjustlocalvars,
// createlabel, and so on, so the checks fail at the same moments and with
// the same messages (G3.13-G3.15, G8, G10).
//
// Errors come back as the bare message text; the parser adds the chunk name
// and line (they are semantic errors, without "near", G9.5).
class Scopes {
public:
    // Names are compared through the Ast's text buffer.
    explicit Scopes(const Ast& ast) : ast_(ast) {}

    // The most locals one function may declare (Lua's MAXVARS).
    static constexpr std::size_t max_locals = 200;

    // ---- Functions ----

    // Starts the main chunk: line 0, with _ENV (named by `env_name`) as its
    // one upvalue, as Lua's loader provides (G2.4).
    void open_main_function(TextRange env_name);

    // Starts a function and its outermost block. `line` is where it is
    // defined.
    void open_function(int line);

    // Ends the current function and returns its upvalues. Fails if a goto or
    // break in it was never resolved: "no visible label 'x' for <goto> at
    // line N" or "break outside loop at line N" (G8.2, G8.6).
    std::expected<std::vector<UpvalueDesc>, std::string> close_function();

    // The line of the current function, 0 for the main chunk.
    [[nodiscard]] int function_line() const { return functions_.back().line; }

    // ---- Blocks ----

    // Starts a block inside the current function. A loop block is where
    // `break` jumps to.
    void open_block(bool is_loop);

    // Ends the current block: its locals and labels go out of scope, the
    // breaks of a loop are resolved, and other pending gotos move out to the
    // enclosing block.
    void close_block();

    // ---- Local variables ----

    // Declares a local, not visible yet (G3.13). Returns false when the
    // function already has max_locals declared.
    [[nodiscard]] bool declare(TextRange name, LocalName::Attribute attribute);

    // Makes the last `count` declared locals visible.
    void activate(std::size_t count);

    // What a name refers to at this point (G5.6).
    struct Lookup {
        enum class Kind : std::uint8_t { Local, Upvalue, Global };
        Kind kind = Kind::Global;
        LocalName::Attribute attribute = LocalName::Attribute::None;
    };

    [[nodiscard]] Lookup lookup(std::string_view name) const;

    // Resolves a name for the tree (G5.6), like Lua's singlevaraux(): a local
    // of the current function gives its slot; a local of an enclosing
    // function becomes an upvalue here (and in every function in between,
    // each capturing it from its parent); anything else is a global. (The
    // compiler checks the limit of 255 upvalues: a <const> local with a
    // constant value is no upvalue for it, so the tree may list more.)
    VariableRef resolve(TextRange name);

    // ---- goto, labels and break ----

    // Creates a label (G8.1). `last` is true when only the end of the block
    // follows it, which puts it outside the scope of the block's locals
    // (G8.5). Fails if the label is already visible (G8.3), or if a pending
    // goto would jump into the scope of a local (G8.4).
    std::expected<void, std::string> add_label(TextRange name, int line, bool last);

    // A goto: resolved now if its label is already visible (a backward jump),
    // otherwise pending until the label appears or the function ends.
    std::expected<void, std::string> add_goto(TextRange name, int line);

    // A break: pending until the enclosing loop block closes.
    std::expected<void, std::string> add_break(int line);

    // The most labels, and pending gotos, the open functions may have
    // (luaM_growvector's limit of SHRT_MAX in newlabelentry), and Lua's
    // message, which has no position.
    static constexpr std::size_t max_labels = 32767;
    static constexpr std::string_view too_many_labels = "too many labels/gotos (limit is 32767)";

private:
    struct Variable {
        TextRange name;
        LocalName::Attribute attribute = LocalName::Attribute::None;
    };

    // Labels and gotos keep their name's hash, so the searches through them
    // compare numbers first (a function may have thousands).
    struct Label {
        TextRange name;
        std::size_t hash = 0;
        int line = 0;
        std::size_t active = 0; // locals active at the label (Lua's nactvar)
    };

    struct Goto {
        TextRange name; // unused for a break
        std::size_t hash = 0;
        int line = 0;
        std::size_t active = 0; // locals active at the goto
        bool is_break = false;
    };

    struct Block {
        std::size_t first_active; // locals active when the block started
        std::size_t first_label;  // index in labels_ of the block's first label
        std::size_t first_goto;   // index in gotos_ of the block's first pending goto
        bool is_loop;
    };

    struct Function {
        int line;
        std::size_t first_variable; // index in variables_ of its first local
        std::size_t active = 0;     // how many of its locals are visible
        std::size_t first_label;
        std::vector<Block> blocks;
        std::vector<UpvalueDesc> upvalues; // in order of first use
    };

    [[nodiscard]] bool same_name(TextRange a, TextRange b) const;

    // resolve() for function `level` (0 is the main chunk). Returns nothing
    // for a global.
    std::optional<VariableRef> resolve_in(std::size_t level, TextRange name);

    // The first visible label of the current function with this name.
    [[nodiscard]] const Label* find_label(TextRange name, std::size_t hash) const;
    [[nodiscard]] std::size_t hash_of(TextRange name) const;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): owned by the parser
    const Ast& ast_;

    std::vector<Function> functions_; // innermost last
    std::vector<Variable> variables_; // the declared locals of every open function
    std::vector<Label> labels_;       // the visible labels of every open function
    std::vector<Goto> gotos_;         // pending gotos and breaks
};

} // namespace cpplua
