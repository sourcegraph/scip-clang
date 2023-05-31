#ifndef SCIP_CLANG_STABLE_FILE_ID_H
#define SCIP_CLANG_STABLE_FILE_ID_H

#include <compare>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"

#include "indexer/Comparison.h"
#include "indexer/Derive.h"
#include "indexer/Path.h"

namespace scip_clang {

/// An identifier for a file that is stable across indexing runs,
/// represented as a path.
///
/// There are 4 kinds of files:
/// 1. In-project files.
/// 2. Generated files: These are present in the build root,
///    but not in the project root.
/// 3. External files: From libraries (stdlib, SDKs etc.)
/// 4. Magic files: Corresponding to the builtin header,
///    and command-line arguments.
///
/// For generated files and magic files, we make up fake paths
/// that are likely to be distinct from actual in-project paths.
///
/// For external files, if available, we track package information
/// in the packageId field of \c FileMetadata. In that case,
/// the path represents the "true" in-project path of the external
/// file. In the absence of package information, the path is
/// fake for external files too.
struct StableFileId {
  RootRelativePathRef path;
  /// Does this file belong to the project being indexed?
  bool isInProject;
  /// Was this path synthesized by truncating an actual path?
  bool isSynthetic;

  template <typename H> friend H AbslHashValue(H h, const StableFileId &s) {
    return H::combine(std::move(h), s.path, s.isInProject, s.isSynthetic);
  }

  DERIVE_CMP_ALL(StableFileId)
};

struct PackageId {
  std::string_view name;
  std::string_view version;
};

/// Summary information about a package.
struct PackageMetadata {
  PackageId id;
  AbsolutePathRef rootPath;
  bool isMainPackage;
};

/// Represents important metadata related to a file.
///
/// The possible cases are:
/// 1. stableFileId.isInProject = true
///    - stableFileId.isSynthetic = false
///    - packageInfo MAY be present.
/// 2. isInProject = false, stableFileId.isSynthetic = false
///    - packageInfo MUST be present.
///    - stableFileId.path is a path relative to an actual root directory.
/// 3. stableFileId.isInProject = false, stableFileId.isSynthetic = true
///    - packageInfo MUST be absent.
///    - stableFileId.path is a fake path
struct FileMetadata {
  StableFileId stableFileId;
  AbsolutePathRef originalPath;
  std::optional<PackageMetadata> packageInfo;

  PackageId packageId() const {
    if (this->packageInfo.has_value()) {
      return this->packageInfo->id;
    }
    return PackageId{};
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_STABLE_FILE_ID_H