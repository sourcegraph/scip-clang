#ifndef SCIP_CLANG_PATH_H
#define SCIP_CLANG_PATH_H

#include <filesystem>
#include <optional>
#include <string>

#include "llvm/ADT/StringRef.h"

namespace scip_clang {

// Non-owning absolute path which is not necessarily null-terminated.
// Meant to be used together with an interner like llvm::StringSaver.
class AbsolutePath {
  std::string_view _data;

  AbsolutePath(std::string_view data) : _data(data) {}

public:
  AbsolutePath() = delete;
  AbsolutePath(const AbsolutePath &) = default;
  AbsolutePath &operator=(const AbsolutePath &) = default;
  AbsolutePath(AbsolutePath &&) = default;
  AbsolutePath &operator=(AbsolutePath &&) = default;

  static std::optional<AbsolutePath> tryFrom(std::string_view path);
  static std::optional<AbsolutePath> tryFrom(llvm::StringRef path) {
    return AbsolutePath::tryFrom(std::string_view(path.data(), path.size()));
  }

  template <typename H> friend H AbslHashValue(H h, const AbsolutePath &x) {
    return H::combine(std::move(h), x._data);
  }

  friend bool operator==(const AbsolutePath &lhs, const AbsolutePath &rhs) {
    return lhs.data() == rhs.data();
  }
  friend bool operator!=(const AbsolutePath &lhs, const AbsolutePath &rhs) {
    return !(lhs == rhs);
  }

  std::string_view data() const {
    return this->_data;
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_PATH_H