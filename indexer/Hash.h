#ifndef SCIP_CLANG_HASH_H
#define SCIP_CLANG_HASH_H

#include <cstdint>
#include <string_view>
#include <utility>

#include "absl/hash/hash.h"
#include "wyhash.h"

#include "indexer/Derive.h"

namespace scip_clang {

struct HashValue {
  uint64_t rawValue;

  void mix(const uint8_t *key, size_t data) {
    this->rawValue = wyhash(key, data, this->rawValue, /*default secret*/ _wyp);
  };

  static uint64_t forText(std::string_view text) {
    HashValue v{0};
    v.mix(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    return v.rawValue;
  }

  DERIVE_HASH_1(HashValue, self.rawValue)
  DERIVE_CMP_ALL(HashValue)
};

} // namespace scip_clang

#endif // SCIP_CLANG_HASH_H