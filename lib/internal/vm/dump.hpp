#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "vm/proto.hpp"

namespace cpplua {

class Heap;
class LuaClosure;

// Binary chunks, in Lua 5.4's format (lundump.h; docs/final_docs/dump.md).

// luaU_dump: the main function `main` as a binary chunk; `strip` leaves out
// the debug information (lines, names, the source).
std::string dump(const Proto& main, bool strip);

// luaU_undump: a closure of the binary chunk `bytes` (which starts with the
// signature, "\x1bLua"), its upvalues closed and nil; or the error
// ("name: bad binary format (truncated chunk)"). `source_name` names the
// chunk, as load's chunk name does.
std::expected<LuaClosure*, std::string> undump(Heap& heap, std::string_view bytes,
                                               std::string_view source_name);

} // namespace cpplua
