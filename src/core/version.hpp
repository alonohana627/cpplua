#pragma once

#include <string_view>

namespace lua2 {

// The banner `lua -v` prints. A placeholder so the build, the executable and
// the tests have one real function to exercise end to end.
std::string_view version();

} // namespace lua2
