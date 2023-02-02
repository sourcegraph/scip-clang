#ifndef SCIP_CLANG_TEST_SNAPSHOT_H
#define SCIP_CLANG_TEST_SNAPSHOT_H

#include <optional>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/raw_ostream.h"

#include "scip/scip.pb.h"

#include "indexer/FileSystem.h"
#include "indexer/Path.h"

namespace scip_clang {
namespace test {

std::string readFileToString(const StdPath &);

bool isTuMainFilePath(std::string_view);

struct FormatOptions {
  bool showDocs;
};

void formatSnapshot(const scip::Document &document,
                    AbsolutePathRef sourceFilePath, FormatOptions options,
                    llvm::raw_ostream &out);

enum class SnapshotMode {
  Compare,
  Update,
};

void compareOrUpdateSingleFile(SnapshotMode mode, std::string_view actual,
                               const StdPath &snapshotFilePath);

// Perform a line-wise diff of expected vs actual.
//
// NOTE(ref: based-on-sorbet): This function implementation was originally
// in Sorbet's expectations.cc named as CHECK_EQ_DIFF
void compareDiff(std::string_view expected, std::string_view actual,
                 std::string_view errorMessage);

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
          const RootPath &rootInSandbox, RootRelativePathRef tuFileInSandbox,
          std::vector<std::string> &&commandLine)>;

  void run(SnapshotMode, RunCompileCommandCallback);
};

} // namespace test
} // namespace scip_clang

#endif // SCIP_CLANG_TEST_SNAPSHOT_H