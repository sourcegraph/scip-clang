#ifndef SCIP_CLANG_TEST_SNAPSHOT_H
#define SCIP_CLANG_TEST_SNAPSHOT_H

#include <optional>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Tooling/CompilationDatabase.h"

#include "indexer/FileSystem.h"
#include "indexer/Path.h"

namespace scip_clang {
namespace test {

std::string readFileToString(const StdPath &);

enum class SnapshotMode {
  Compare,
  Update,
};

bool isTuMainFilePath(std::string_view);

class MultiTuSnapshotTest final {
  RootPath rootPath;
  struct InputOutput {
    RootRelativePath sourceFilePath;
    RootRelativePath snapshotPath;
  };

  std::vector<InputOutput> inputOutputs;

public:
  MultiTuSnapshotTest(RootPath &&,
                      absl::FunctionRef<std::optional<RootRelativePath>(
                          const RootRelativePath &)>
                          getSnapshotPath);

  using RunCompileCommandCallback =
      absl::FunctionRef<absl::flat_hash_map<RootRelativePath, std::string>(
          clang::tooling::CompileCommand &&command)>;

  void run(SnapshotMode, RunCompileCommandCallback);
};

void compareOrUpdateSingleFile(SnapshotMode mode, std::string_view actual,
                               const StdPath &snapshotFilePath);

// Perform a line-wise diff of expected vs actual.
//
// NOTE(ref: based-on-sorbet): This function implementation was originally
// in Sorbet's expectations.cc named as CHECK_EQ_DIFF
void compareDiff(std::string_view expected, std::string_view actual,
                 std::string_view errorMessage);

} // namespace test
} // namespace scip_clang

#endif // SCIP_CLANG_TEST_SNAPSHOT_H