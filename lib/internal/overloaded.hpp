#pragma once

namespace lua2 {

// Builds one callable out of several lambdas, for std::visit:
//
//   std::visit(Overloaded{
//       [](const IntegerExpr& node) { ... },
//       [](const StringExpr& node) { ... },
//   }, expr);
//
// With no catch-all lambda, a variant alternative without a matching lambda
// is a compile error, so every node type must be handled.
template <typename... Lambdas>
struct Overloaded : Lambdas... { // NOLINT(misc-multiple-inheritance): the point of the idiom
    using Lambdas::operator()...;
};

} // namespace lua2
