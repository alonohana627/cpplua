#include <print>

#include "core/version.hpp"

int main() {
  std::println("{}", lua2::version());
  return 0;
}
