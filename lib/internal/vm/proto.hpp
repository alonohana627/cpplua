#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gc/object.hpp"
#include "value/string.hpp"
#include "value/value.hpp"
#include "vm/opcodes.hpp"

namespace cpplua {

// What the compiler makes of one function (Lua's Proto): its code and what
// the code refers to. Running it needs a closure (vm/function.hpp), which
// adds the upvalues' values. Protos are collectable: closures and the
// enclosing proto keep them alive.
struct Proto : GcObject {
    // How a closure finds each upvalue when it is created (Lua's Upvaldesc).
    struct UpvalueInfo {
        const String* name = nullptr; // for messages and listings
        bool in_stack = false;        // a register of the enclosing function, or
        std::uint8_t index = 0;       // one of its upvalues; which one
        std::uint8_t kind = 0;        // the variable's kind: regular, const, close (for dumps)
    };

    // A local variable's name and the instructions where it is active (Lua's
    // LocVar), for messages (E8.2) and listings.
    struct LocalInfo {
        const String* name = nullptr;
        int start_pc = 0; // first instruction where the variable is active
        int end_pc = 0;   // first instruction where it is not
    };

    Proto() { type = ObjectType::Proto; }

    std::vector<Instruction> code;
    std::vector<Value> constants; // nil, booleans, numbers and strings
    std::vector<Proto*> protos;   // the functions defined inside
    std::vector<UpvalueInfo> upvalues;
    std::vector<int> lines; // the source line of each instruction
    std::vector<LocalInfo> locals;

    const String* source = nullptr; // the chunk name, as Lua keeps it ("@file", "=stdin")
    int line_defined = 0;           // 0 for the main chunk
    int last_line_defined = 0;
    std::uint8_t parameter_count = 0;
    bool is_vararg = false;
    std::uint8_t max_stack_size = 2; // registers needed, at least 2 (Lua's minimum)

    GcObject* gc_list = nullptr;   // the collector's gray lists
    std::size_t counted_bytes = 0; // what the vectors added to the heap's count

    // The memory the vectors hold, as counted toward the collector's pacing.
    [[nodiscard]] std::size_t vector_bytes() const {
        return (code.capacity() * sizeof(Instruction)) + (constants.capacity() * sizeof(Value)) +
               (protos.capacity() * sizeof(Proto*)) + (upvalues.capacity() * sizeof(UpvalueInfo)) +
               (lines.capacity() * sizeof(int)) + (locals.capacity() * sizeof(LocalInfo));
    }
};

// The proto as text, in the style of `luac -l -l`: a header, one line per
// instruction with its source line, operands and a comment, then the
// constants, locals and upvalues; then the nested functions, the same way.
std::string list_proto(const Proto& proto);

// The chunk name as messages show it (Lua's luaO_chunkid): "@file.lua" is
// file.lua, "=stdin" is stdin, and source text is [string "first line..."].
std::string chunk_id(std::string_view source);

} // namespace cpplua
