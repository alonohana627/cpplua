#pragma once

#include <span>

namespace cpplua {

// The stand-alone interpreter, as the real `lua` program (lua.c): runs
// `argv` (argv[0] the program's name) and returns the exit status. Options
// -e stat, -l mod, -l g=mod, -i, -v, -E, -W, --, - ; then a script and its
// arguments (in the global `arg`); LUA_INIT_5_4 or LUA_INIT; and the
// interactive loop when there is nothing else to do and stdin is a
// terminal. The collector runs in generational mode, as lua.c sets it,
// unless `generational` is false.
int run_standalone(std::span<char* const> argv, bool generational = true);

} // namespace cpplua
