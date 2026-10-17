#include "parser/scopes.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace cpplua {

// ---- Functions ----

void Scopes::open_main_function(TextRange env_name) {
    open_function(0);
    // The loader gives the main chunk _ENV as its upvalue 0 (Lua's mainfunc).
    functions_.back().upvalues.push_back(
        UpvalueDesc{.name = env_name, .from_parent_local = true, .index = 0});
}

void Scopes::open_function(int line) {
    functions_.push_back(Function{
        .line = line,
        .first_variable = variables_.size(),
        .active = 0,
        .first_label = labels_.size(),
        .blocks = {},
        .upvalues = {},
    });
    open_block(false); // the function's outermost block
}

std::expected<std::vector<UpvalueDesc>, std::string> Scopes::close_function() {
    const std::size_t first_goto = functions_.back().blocks.front().first_goto;
    close_block(); // the outermost block, the only one left

    // Gotos that reached the function's outermost block without finding
    // their label (Lua's undefgoto). The first one is reported.
    std::expected<std::vector<UpvalueDesc>, std::string> result =
        std::move(functions_.back().upvalues);
    if (first_goto < gotos_.size()) {
        const Goto& pending = gotos_[first_goto];
        result = std::unexpected(pending.is_break
                                     ? std::format("break outside loop at line {}", pending.line)
                                     : std::format("no visible label '{}' for <goto> at line {}",
                                                   ast_.text(pending.name), pending.line));
    }

    // Leave the state consistent even after an error.
    const Function& function = functions_.back();
    variables_.resize(function.first_variable);
    labels_.resize(function.first_label);
    gotos_.resize(std::min(gotos_.size(), first_goto));
    functions_.pop_back();
    return result;
}

// ---- Blocks ----

void Scopes::open_block(bool is_loop) {
    Function& function = functions_.back();
    function.blocks.push_back(Block{
        .first_active = function.active,
        .first_label = labels_.size(),
        .first_goto = gotos_.size(),
        .is_loop = is_loop,
    });
}

void Scopes::close_block() {
    Function& function = functions_.back();
    const Block block = function.blocks.back();
    function.blocks.pop_back();

    // The block's locals go out of scope.
    function.active = block.first_active;
    variables_.resize(function.first_variable + function.active);

    // A loop resolves its breaks: they jump to just after the loop.
    if (block.is_loop) {
        const auto breaks = std::ranges::remove_if(
            gotos_.begin() + static_cast<std::ptrdiff_t>(block.first_goto), gotos_.end(),
            [](const Goto& pending) { return pending.is_break; });
        gotos_.erase(breaks.begin(), breaks.end());
    }

    // The block's labels go out of scope.
    labels_.resize(block.first_label);

    // Gotos still pending move out to the enclosing block, which they now
    // leave from the enclosing block's level (Lua's movegotosout).
    if (!function.blocks.empty()) {
        for (Goto& pending : gotos_ | std::views::drop(block.first_goto)) {
            pending.active = block.first_active;
        }
    }
}

// ---- Local variables ----

bool Scopes::declare(TextRange name, LocalName::Attribute attribute) {
    const Function& function = functions_.back();
    if (variables_.size() - function.first_variable >= max_locals) {
        return false;
    }
    variables_.push_back(Variable{.name = name, .attribute = attribute});
    return true;
}

void Scopes::activate(std::size_t count) {
    functions_.back().active += count;
}

Scopes::Lookup Scopes::lookup(std::string_view name) const {
    // Innermost function first; within it, the most recent local first, so
    // an inner declaration shadows an outer one.
    for (std::size_t f = functions_.size(); f-- > 0;) {
        const Function& function = functions_[f];
        for (std::size_t i = function.first_variable + function.active;
             i-- > function.first_variable;) {
            if (ast_.text(variables_[i].name) == name) {
                return Lookup{
                    .kind =
                        f + 1 == functions_.size() ? Lookup::Kind::Local : Lookup::Kind::Upvalue,
                    .attribute = variables_[i].attribute,
                };
            }
        }
    }
    return Lookup{.kind = Lookup::Kind::Global, .attribute = LocalName::Attribute::None};
}

std::expected<VariableRef, Scopes::TooManyUpvalues> Scopes::resolve(TextRange name) {
    auto found = resolve_in(functions_.size() - 1, name);
    if (!found) {
        return std::unexpected(found.error());
    }
    return found->value_or(VariableRef{.kind = VariableRef::Kind::Global, .index = 0});
}

std::expected<std::optional<VariableRef>, Scopes::TooManyUpvalues>
Scopes::resolve_in(std::size_t level, TextRange name) {
    Function& function = functions_[level];

    // 1. A visible local of this function, the most recent first.
    for (std::size_t i = function.first_variable + function.active;
         i-- > function.first_variable;) {
        if (same_name(variables_[i].name, name)) {
            return VariableRef{
                .kind = VariableRef::Kind::Local,
                .index = static_cast<std::uint32_t>(i - function.first_variable),
            };
        }
    }

    // 2. An upvalue this function already has.
    for (std::size_t i = 0; i < function.upvalues.size(); ++i) {
        if (same_name(function.upvalues[i].name, name)) {
            return VariableRef{
                .kind = VariableRef::Kind::Upvalue,
                .index = static_cast<std::uint32_t>(i),
            };
        }
    }

    // 3. Something of an enclosing function: if it is a local or an upvalue
    //    there, it becomes a new upvalue here. The main chunk has no
    //    enclosing function, so there a name not found is a global.
    if (level == 0) {
        return std::nullopt;
    }
    auto outer = resolve_in(level - 1, name);
    if (!outer || !*outer) {
        return outer; // an error, or a global
    }
    if (function.upvalues.size() >= max_upvalues) {
        return std::unexpected(TooManyUpvalues{.function_line = function.line});
    }
    function.upvalues.push_back(UpvalueDesc{
        .name = name,
        .from_parent_local = (*outer)->kind == VariableRef::Kind::Local,
        .index = (*outer)->index,
    });
    return VariableRef{
        .kind = VariableRef::Kind::Upvalue,
        .index = static_cast<std::uint32_t>(function.upvalues.size() - 1),
    };
}

// ---- goto, labels and break ----

bool Scopes::same_name(TextRange a, TextRange b) const {
    return ast_.text(a) == ast_.text(b);
}

const Scopes::Label* Scopes::find_label(TextRange name) const {
    for (std::size_t i = functions_.back().first_label; i < labels_.size(); ++i) {
        if (same_name(labels_[i].name, name)) {
            return &labels_[i];
        }
    }
    return nullptr;
}

std::expected<void, std::string> Scopes::add_label(TextRange name, int line, bool last) {
    if (const Label* existing = find_label(name)) {
        return std::unexpected(
            std::format("label '{}' already defined on line {}", ast_.text(name), existing->line));
    }

    const Function& function = functions_.back();
    const Block& block = function.blocks.back();
    // A label at the end of its block counts as outside the block's locals.
    const std::size_t active = last ? block.first_active : function.active;
    labels_.push_back(Label{.name = name, .line = line, .active = active});

    // Resolve the pending gotos of this block that jump to it (Lua's
    // solvegotos). A goto with fewer locals active than the label would jump
    // into the scope of the first extra local.
    for (std::size_t i = block.first_goto; i < gotos_.size();) {
        const Goto& pending = gotos_[i];
        if (pending.is_break || !same_name(pending.name, name)) {
            ++i;
            continue;
        }
        if (pending.active < active) {
            const Variable& entered = variables_[function.first_variable + pending.active];
            return std::unexpected(
                std::format("<goto {}> at line {} jumps into the scope of local '{}'",
                            ast_.text(name), pending.line, ast_.text(entered.name)));
        }
        gotos_.erase(gotos_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return {};
}

void Scopes::add_goto(TextRange name, int line) {
    if (find_label(name) != nullptr) {
        return; // a backward jump to a visible label is always fine
    }
    gotos_.push_back(
        Goto{.name = name, .line = line, .active = functions_.back().active, .is_break = false});
}

void Scopes::add_break(int line) {
    Goto pending; // a break has no label name
    pending.line = line;
    pending.active = functions_.back().active;
    pending.is_break = true;
    gotos_.push_back(pending);
}

} // namespace cpplua
