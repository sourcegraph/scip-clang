#include <cstring>
#include <string>
#include <string_view>

#include "indexer/Comparison.h"

namespace cmp {

Comparison compareStrings(std::string_view s1, std::string_view s2) {
  auto s1size = s1.size();
  auto s2size = s2.size();
  if (s1size < s2size) {
    return cmp::Less;
  }
  if (s1size > s2size) {
    return cmp::Greater;
  }
  auto cmp = std::memcmp(s1.data(), s2.data(), s2size);
  if (cmp < 0) {
    return cmp::Less;
  } else if (cmp == 0) {
    return cmp::Equal;
  }
  return cmp::Greater;
}

std::strong_ordering operator<=>(const CmpStr &s1, const CmpStr &s2) {
  return cmp::comparisonToStrongOrdering(cmp::compareStrings(s1.sv, s2.sv));
}

} // namespace cmp