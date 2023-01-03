#ifndef SCIP_CLANG_HASH_H
#define SCIP_CLANG_HASH_H

#include <cstdint>
#include <utility>

#include "absl/hash/hash.h"
#include "wyhash.h"

#include "indexer/Derive.h"

namespace scip_clang {

struct HashValue {
  uint64_t rawValue;

  HashValue mix(const uint8_t *key, size_t data) {
    return HashValue{
        wyhash(key, data, this->rawValue, /*default secret*/ _wyp)};
  };

  DERIVE_HASH_CMP_1(HashValue, self.rawValue)
};

} // namespace scip_clang

#endif // SCIP_CLANG_HASH_H