#ifndef SCIP_CLANG_PATH_H
#define SCIP_CLANG_PATH_H

#include <filesystem>
#include <optional>
#include <string>

#include "llvm/ADT/StringRef.h"

#include "indexer/Derive.h"

namespace scip_clang {

// Non-owning absolute path which is not necessarily null-terminated.
// Meant to be used together with an interner like llvm::StringSaver.
class AbsolutePathRef {
  std::string_view _data;

  AbsolutePathRef(std::string_view data) : _data(data) {}

public:
  AbsolutePathRef() = delete;
  AbsolutePathRef(const AbsolutePathRef &) = default;
  AbsolutePathRef &operator=(const AbsolutePathRef &) = default;
  AbsolutePathRef(AbsolutePathRef &&) = default;
  AbsolutePathRef &operator=(AbsolutePathRef &&) = default;

  static std::optional<AbsolutePathRef> tryFrom(std::string_view path);
  static std::optional<AbsolutePathRef> tryFrom(llvm::StringRef path) {
    return AbsolutePathRef::tryFrom(std::string_view(path.data(), path.size()));
  }

  DERIVE_HASH_EQ_1(AbsolutePathRef, self._data)

  std::string_view data() const {
    return this->_data;
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_PATH_H