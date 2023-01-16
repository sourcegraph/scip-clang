#include <cstring>
#include <string_view>

#include "indexer/Comparison.h"

namespace scip_clang {

Comparison compareStrings(std::string_view s1, std::string_view s2) {
  auto s1size = s1.size();
  auto s2size = s2.size();
  if (s1size < s2size) {
    return Comparison::Less;
  }
  if (s1size > s2size) {
    return Comparison::Greater;
  }
  auto cmp = std::memcmp(s2.data(), s2.data(), s2size);
  if (cmp < 0) {
    return Comparison::Less;
  } else if (cmp == 0) {
    return Comparison::Equal;
  }
  return Comparison::Greater;
}

} // namespace scip_clang