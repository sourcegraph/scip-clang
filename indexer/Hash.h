#ifndef SCIP_CLANG_HASH_H
#define SCIP_CLANG_HASH_H

#include <cstdint>
#include <utility>

#include "absl/hash/hash.h"
#include "wyhash.h"

namespace scip_clang {

struct HashValue {
  uint64_t rawValue;

  HashValue mix(const uint8_t *key, size_t data) {
    return HashValue{
        wyhash(key, data, this->rawValue, /*default secret*/ _wyp)};
  };

  template <typename H> friend H AbslHashValue(H h, const HashValue &hv) {
    return H::combine(std::move(h), hv.rawValue);
  }
  friend bool operator==(const HashValue &lhs, const HashValue &rhs) {
    return lhs.rawValue == rhs.rawValue;
  }
  friend bool operator!=(const HashValue &lhs, const HashValue &rhs) {
    return !(lhs == rhs);
  }
  friend bool operator<(const HashValue &lhs, const HashValue &rhs) {
    return lhs.rawValue < rhs.rawValue;
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_HASH_H