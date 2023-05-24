#ifndef SCIP_CLANG_PATH_H
#define SCIP_CLANG_PATH_H

#include <compare>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/JSON.h"

#include "indexer/Comparison.h"
#include "indexer/Derive.h"
#include "indexer/Enforce.h"

namespace scip_clang {

template <typename T>
class PathPrefixIterator
    : llvm::iterator_facade_base<PathPrefixIterator<T>, std::input_iterator_tag,
                                 const T> {
  std::optional<T> data;

  using Self = PathPrefixIterator<T>;
  using Base =
      llvm::iterator_facade_base<Self, std::input_iterator_tag, const T>;

public:
  explicit PathPrefixIterator<T>(std::optional<T> data) : data(data) {}
  bool operator==(const Self &other) const {
    return this->data == other.data;
  }

  typename Base::reference operator*() const {
    return this->data.value();
  }

  friend T;

  Self &operator++() {
    this->data = this->data->prefix();
    return *this;
  }
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

  /// Try to get the file name by slicing off the prefix till the last
  /// path separator.
  std::optional<std::string_view> fileName() const;

  bool isNormalized() const;

  void normalize(llvm::SmallVectorImpl<char> &newStorage) const;

  PathPrefixIterator<AbsolutePathRef> prefixesBegin() const {
    return PathPrefixIterator<AbsolutePathRef>{*this};
  }
  PathPrefixIterator<AbsolutePathRef> prefixesEnd() const {
    return PathPrefixIterator<AbsolutePathRef>{std::nullopt};
  }

  // Generally this should be avoided in favor
  std::optional<AbsolutePathRef> prefix() const;
};

/// Typically used when referring to paths for files which may or may not
/// be inside the project root. Otherwise, \c RootRelativePathRef
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

  DERIVE_HASH_CMP_NEWTYPE(AbsolutePath, value, CMP_STR)
  static llvm::json::Value toJSON(const AbsolutePath &);
  static bool fromJSON(const llvm::json::Value &value, AbsolutePath &,
                       llvm::json::Path path);
};
SERIALIZABLE(AbsolutePath)

enum class RootKind : uint8_t {
  Project,
  Build,
  External,
};

class RootRelativePathRef {
  std::string_view value; // may be empty
  RootKind _kind;

public:
  RootRelativePathRef() = default;
  explicit RootRelativePathRef(std::string_view, RootKind kind);

  std::string_view asStringView() const {
    return this->value;
  }

  RootKind kind() const {
    return this->_kind;
  }

  std::string_view extension() const;

  template <typename H>
  friend H AbslHashValue(H h, const RootRelativePathRef &self) {
    return H::combine(std::move(h), self.value, bool(self._kind));
  }

  friend std::strong_ordering operator<=>(const RootRelativePathRef &,
                                          const RootRelativePathRef &);
  DERIVE_EQ_VIA_CMP(RootRelativePathRef)

  /// Try to get the file name by slicing off the prefix till the last
  /// path separator.
  std::optional<std::string_view> fileName() const;
};

class RootRelativePath {
  std::string value;
  RootKind _kind;

public:
  RootRelativePath() = default;
  RootRelativePath(RootRelativePath &&) = default;
  RootRelativePath &operator=(RootRelativePath &&) = default;
  RootRelativePath(const RootRelativePath &) = default;
  RootRelativePath &operator=(const RootRelativePath &) = default;

  explicit RootRelativePath(RootRelativePathRef ref);

  RootRelativePath(std::string &&, RootKind);

  const std::string &asStringRef() const {
    return this->value;
  }

  RootRelativePathRef asRef() const {
    return RootRelativePathRef(this->value, this->_kind);
  }

  void replaceExtension(std::string_view newExtension);

  DERIVE_HASH_CMP_NEWTYPE(RootRelativePath, asRef(), CMP_EXPR)
};

class RootPath final {
  AbsolutePath value;
  RootKind _kind;

public:
  explicit RootPath(AbsolutePath &&value, RootKind kind)
      : value(std::move(value)), _kind(kind) {}

  AbsolutePathRef asRef() const {
    return this->value.asRef();
  }

  RootKind kind() const {
    return this->_kind;
  }

  /// If the result is non-null, it points to the storage of
  /// \p maybePathInsideProject
  std::optional<RootRelativePathRef>
  tryMakeRelative(AbsolutePathRef maybePathInsideProject) const;

  AbsolutePath makeAbsolute(RootRelativePathRef) const;
  AbsolutePath makeAbsoluteAllowKindMismatch(RootRelativePathRef) const;
};

} // namespace scip_clang

#endif // SCIP_CLANG_PATH_H
