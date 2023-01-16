#ifndef SCIP_CLANG_PATH_H
#define SCIP_CLANG_PATH_H

#include <optional>
#include <string>
#include <string_view>

#include "llvm/ADT/StringRef.h"

#include "indexer/Derive.h"
#include "indexer/Enforce.h"

namespace scip_clang {

class ProjectRootRelativePathRef {
  std::string_view _data;

public:
  ProjectRootRelativePathRef(std::string_view data) : _data(data) {
    ENFORCE(data.size() > 0);
    ENFORCE(data.front() != '/');
  }
  std::string_view data() const {
    return this->_data;
  }

  DERIVE_HASH_EQ_1(ProjectRootRelativePathRef, self._data)
};

class AbsolutePath;

// Non-owning absolute path which is not necessarily null-terminated.
// Meant to be used together with an interner like llvm::StringSaver.
class AbsolutePathRef {
  std::string_view _data;

  AbsolutePathRef(std::string_view data) : _data(data) {}
  friend AbsolutePath;

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

  // Basic prefix-based implementation; does not handle lexical normalization.
  std::optional<std::string_view> makeRelative(AbsolutePathRef longerPath);
};

class AbsolutePath {
  std::string _data;

public:
  AbsolutePath(AbsolutePathRef ref) : _data(ref.data()) {}
  AbsolutePathRef asRef() const {
    return AbsolutePathRef(std::string_view(this->_data));
  }
};

// std::filesystem::path APIs allocate all over the place, and neither
// LLVM nor Abseil have a dedicated path type, so roll our own. Sigh.
class Path final {
  std::string _data;

public:
  Path() = default;
  Path(Path &&) = default;
  Path &operator=(Path &&) = default;
  Path(const Path &) = delete;
  Path &operator=(const Path &) = delete;

  Path(std::string data) : _data(data) {}
  const std::string_view asStringView() const {
    return std::string_view(this->_data);
  }
  const std::string_view filename() const;
};

} // namespace scip_clang

#endif // SCIP_CLANG_PATH_H