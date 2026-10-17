#pragma once

#include <string>

namespace lua2 {

// An error raised while Lua code runs, such as `attempt to divide by zero`.
// Like SyntaxError, it is returned inside std::expected, never thrown
// (docs/plan/values.md, part 0). For now it is only the message; where the
// error happened, and catching it with pcall, come with the interpreter
// (stages 5 and 8).
struct RuntimeError {
    std::string message;
};

} // namespace lua2
