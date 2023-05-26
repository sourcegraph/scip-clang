#ifndef SCIP_CLANG_PACKAGE_MAP_H
#define SCIP_CLANG_PACKAGE_MAP_H

#include <string_view>

#include "absl/container/flat_hash_map.h"

#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/StringSaver.h"

#include "indexer/FileMetadata.h"
#include "indexer/FileSystem.h"
#include "indexer/Path.h"

namespace scip_clang {

/// Map tracking path->(name, version) which persists across TUs.
///
/// Modifiers internal structures to cache lookups.
class PackageMap final {
  llvm::BumpPtrAllocator storage;
  llvm::UniqueStringSaver interner;
  absl::flat_hash_map<AbsolutePathRef, PackageMetadata> map;

  absl::flat_hash_set<std::string_view> warnedBadPaths;

  RootPath projectRootPath;

  bool isTesting;

public:
  /// Populate a PackageMap from a file with entries in the format:
  ///
  /// {"directory": "<some path>", "package": "name@version" }
  ///
  /// Changes to fallback behavior if packageMapPath is empty.
  PackageMap(const RootPath &projectRootPath, const StdPath &packageMapPath,
             bool isTesting);

  /// Check if we have package information for \p filepath.
  ///
  /// Non-const as it caches intermediate results produced during lookups.
  std::optional<PackageMetadata> lookup(AbsolutePathRef filepath);

private:
  std::string_view store(std::string_view);

  bool checkPathIsNormalized(AbsolutePathRef);

  void populate(const StdPath &packageMapPath);
};

} // namespace scip_clang

#endif