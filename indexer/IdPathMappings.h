#ifndef SCIP_CLANG_ID_PATH_MAPPINGS_H
#define SCIP_CLANG_ID_PATH_MAPPINGS_H

#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"

#include "indexer/FileMetadata.h"
#include "indexer/Hash.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/PackageMap.h"
#include "indexer/Path.h"

namespace scip_clang {

/// Type to retrieve information about the \c clang::FileID corresponding
/// to a (HashValue, Path) pair.
///
/// The worker and driver communicate using (HashValue, Path) pairs,
/// since those are stable across different workers running in parallel.
///
/// However, inside a worker, we'd like to use \c clang::FileID keys if
/// possible (e.g. storing Documents before indexing), since they are
/// 32-bit integer values. This map translates driver->worker info
/// in (HashValue, Path) terms to FileIDs.
///
/// In general, it may be the case that multiple FileIDs correspond to
/// the same (HashValue, Path) pair (this happens for well-behaved
/// headers, c.f. \c FileIndexingPlanner); the representative FileID
/// is chosen arbitrarily.
class ClangIdLookupMap {
  struct Value {
    absl::flat_hash_map<HashValue, clang::FileID> hashToFileId;
  };

  absl::flat_hash_map<AbsolutePathRef, std::shared_ptr<Value>> impl;

public:
  ClangIdLookupMap() = default;

  void insert(AbsolutePathRef absPathRef, HashValue hashValue,
              clang::FileID fileId);

  void forEachPathAndHash(
      absl::FunctionRef<void(AbsolutePathRef, const absl::flat_hash_map<
                                                  HashValue, clang::FileID> &)>
          callback) const;

  std::optional<clang::FileID> lookup(AbsolutePathRef absPathRef,
                                      HashValue hashValue) const;

  std::optional<clang::FileID>
  lookupAnyFileId(AbsolutePathRef absPathRef) const;
};

/// Type to track canonical relative paths for FileIDs.
///
/// Two types of files may not have canonical relative paths:
/// 1. Paths inside the build root without a corresponding file inside
///    the project root.
/// 2. Files from outside the project.
///
/// In the future, for cross-repo, a directory layout<->project mapping
/// may be supplied or inferred, which would provide canonical relative
/// paths for more files.
class FileMetadataMap final {
  using Self = FileMetadataMap;

  std::vector<RootRelativePath> storage;

  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>, FileMetadata>
      map;

  const RootPath &projectRootPath;

  const RootPath &buildRootPath;

  PackageMap &packageMap;

public:
  FileMetadataMap() = delete;
  FileMetadataMap(const RootPath &projectRootPath,
                  const RootPath &buildRootPath, PackageMap &packageMap)
      : map(), projectRootPath(projectRootPath), buildRootPath(buildRootPath),
        packageMap(packageMap) {}
  FileMetadataMap(FileMetadataMap &&other) = default;
  FileMetadataMap &operator=(FileMetadataMap &&) = delete;
  FileMetadataMap(const FileMetadataMap &) = delete;
  FileMetadataMap &operator=(const FileMetadataMap &) = delete;

  void populate(const ClangIdLookupMap &clangIdLookupMap);

  /// Returns true iff a new entry was inserted.
  bool insert(clang::FileID fileId, AbsolutePathRef absPathRef);

  bool contains(clang::FileID fileId) const {
    return this->map.contains({fileId});
  }

  /// See the doc comment on \c FileMetadataMap
  std::optional<StableFileId> getStableFileId(clang::FileID fileId) const;

  /// The return value may be nullptr if the metadata is missing
  const FileMetadata *getFileMetadata(clang::FileID fileId) const;

  void
  forEachFileId(absl::FunctionRef<void(clang::FileID, StableFileId)> callback);
};

} // namespace scip_clang

#endif // SCIP_CLANG_ID_PATH_MAPPINGS_H
