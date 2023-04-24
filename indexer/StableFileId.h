#ifndef SCIP_CLANG_STABLE_FILE_ID_H
#define SCIP_CLANG_STABLE_FILE_ID_H

#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"

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
/// For 2, 3 and 4, we make up fake paths that are likely to be
/// distinct from actual in-project paths.
///
/// In the future, for cross-repo, a directory layout<->project mapping
/// may be supplied or inferred, which would enable us to use non-synthetic
/// paths for external files.
struct StableFileId {
  RootRelativePathRef path;
  bool isInProject;
  /// Track this for debugging.
  bool isSynthetic;

  template <typename H>
  friend H AbslHashValue(H h, const StableFileId &stableFileId) {
    return H::combine(std::move(h), stableFileId.path, stableFileId.isInProject,
                      stableFileId.isSynthetic);
  }

  DERIVE_EQ_ALL(StableFileId)
};

using GetStableFileId =
    absl::FunctionRef<std::optional<StableFileId>(clang::FileID)>;

} // namespace scip_clang

#endif // SCIP_CLANG_STABLE_FILE_ID_H