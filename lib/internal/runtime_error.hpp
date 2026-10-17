#pragma once

#include <string>

namespace cpplua {

// An error raised by an operation on values, such as `attempt to divide by
// zero`. Like SyntaxError, it is returned inside std::expected, never thrown
// (docs/plan/values.md, part 0). It is only the message: the VM adds where
// the error happened when it raises it (vm/execute.cpp).
struct RuntimeError {
    std::string message;
};

} // namespace cpplua
