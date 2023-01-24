#ifndef SCIP_CLANG_COMPARISON_H
#define SCIP_CLANG_COMPARISON_H

#include <compare>
#include <string>
#include <string_view>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#define CMP_CHECK(_cmp_expr)        \
  if (auto c = _cmp_expr; c != 0) { \
    return c;                       \
  }

#define CMP_EXPR(_expr1, _expr2) CMP_CHECK(_expr1 <=> _expr2)

#define CMP_STR(_expr1, _expr2) \
  CMP_EXPR(cmp::CmpStr{_expr1}, cmp::CmpStr{_expr2})

#define CMP_RANGE(_expr1, _expr2) CMP_CHECK(cmp::compareRange(_expr1, _expr2))

namespace cmp {

enum Comparison {
  Greater = 1,
  Equal = 0,
  Less = -1,
};

// Size-first comparison of strings, meant for determinism,
// not for user-facing output.
Comparison compareStrings(std::string_view s1, std::string_view s2);

inline std::strong_ordering comparisonToStrongOrdering(Comparison c) {
  switch (c) {
  case Greater:
    return std::strong_ordering::greater;
  case Equal:
    return std::strong_ordering::equal;
  case Less:
    return std::strong_ordering::less;
  }
}

template <typename C>
static std::strong_ordering compareRange(const C &c1, const C &c2) {
  CMP_EXPR(c1.size(), c2.size());
  for (size_t i = 0; i < static_cast<size_t>(c1.size()); ++i) { //
    CMP_EXPR(c1[i], c2[i]);
  }
  return std::strong_ordering::equal;
}

template <typename C, typename F>
static std::strong_ordering compareRange(const C &c1, const C &c2, F f) {
  CMP_EXPR(c1.size(), c2.size());
  for (size_t i = 0; i < static_cast<size_t>(c1.size()); ++i) {
    CMP_CHECK(f(c1[i], c2[i]));
  }
  return std::strong_ordering::equal;
}

struct CmpStr {
  std::string_view sv;

  friend std::strong_ordering operator<=>(const CmpStr &s1, const CmpStr &s2);
};

} // namespace cmp

#endif // SCIP_CLANG_COMPARISON_H
