#ifndef SCIP_CLANG_PATH_H
#define SCIP_CLANG_PATH_H

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include "indexer/Comparison.h"
#include "indexer/Derive.h"
#include "indexer/Enforce.h"

namespace scip_clang {

class ProjectRootRelativePathRef {
  std::string_view value; // may be empty

public:
  ProjectRootRelativePathRef() = default;
  explicit ProjectRootRelativePathRef(std::string_view);

  std::string_view asStringView() const {
    return this->value;
  }

  DERIVE_HASH_1(ProjectRootRelativePathRef, self.value)
  DERIVE_EQ_ALL(ProjectRootRelativePathRef)
};

class AbsolutePathRef {
  std::string_view value; // is non-empty

  explicit AbsolutePathRef(std::string_view);

public:
  AbsolutePathRef() = delete;
  AbsolutePathRef(const AbsolutePathRef &) = default;
  AbsolutePathRef &operator=(const AbsolutePathRef &) = default;
  AbsolutePathRef(AbsolutePathRef &&) = default;
  AbsolutePathRef &operator=(AbsolutePathRef &&) = default;

  static std::optional<AbsolutePathRef> tryFrom(std::string_view);
  static std::optional<AbsolutePathRef> tryFrom(llvm::StringRef);

  DERIVE_HASH_1(AbsolutePathRef, self.value)
  DERIVE_EQ_ALL(AbsolutePathRef)

  std::string_view asStringView() const {
    return this->value;
  }

  // Basic prefix-based implementation; does not handle lexical normalization.
  std::optional<std::string_view> makeRelative(AbsolutePathRef longerPath);
};

/// Typically used when referring to paths for files which may or may not
/// be inside the project root. Otherwise, \c ProjectRootRelativePathRef
/// should be used instead.
class AbsolutePath {
  std::string value; // non-empty, but allow default constructor for avoiding
                     // PITA as a hashmap key

public:
  AbsolutePath() = default;
  AbsolutePath(AbsolutePath &&) = default;
  AbsolutePath &operator=(AbsolutePath &&) = default;
  // Allow copy constructor+assignment for use in hashmap key +
  // (de)serialization. Avoid calling these otherwise.
  AbsolutePath(const AbsolutePath &) = default;
  AbsolutePath &operator=(const AbsolutePath &) = default;

  explicit AbsolutePath(std::string &&value) : value(std::move(value)) {
    ENFORCE(
        AbsolutePathRef::tryFrom(std::string_view(this->value)).has_value());
  }

  explicit AbsolutePath(AbsolutePathRef ref) : value(ref.asStringView()) {}

  const std::string &asStringRef() const {
    return this->value;
  }

  AbsolutePathRef asRef() const;

  static llvm::json::Value toJSON(const AbsolutePath &);
  static bool fromJSON(const llvm::json::Value &value, AbsolutePath &,
                       llvm::json::Path path);
};
SERIALIZABLE(AbsolutePath)

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