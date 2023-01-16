#ifndef SCIP_CLANG_COMPARISON_H
#define SCIP_CLANG_COMPARISON_H

#include <string_view>

namespace scip_clang {

enum class Comparison {
  Greater,
  Equal,
  Less,
};

// Size-first comparison of strings, meant for determinism,
// not for user-facing output.
Comparison compareStrings(std::string_view s1, std::string_view s2);

} // namespace scip_clang

#endif // SCIP_CLANG_COMPARISON_H