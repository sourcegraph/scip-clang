#ifndef SCIP_CLANG_TEST_SNAPSHOT_H
#define SCIP_CLANG_TEST_SNAPSHOT_H

#include <optional>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "llvm/Support/raw_ostream.h"

#include "scip/scip.pb.h"

#include "indexer/CompilationDatabase.h"
#include "indexer/FileSystem.h"
#include "indexer/Path.h"

namespace scip_clang {
namespace test {

std::string readFileToString(const StdPath &);

bool isTuMainFilePath(std::string_view);

struct FormatOptions {
  bool showDocs;
};

class SnapshotPrinter {
  llvm::raw_ostream &out;
  std::string_view lineStart;
  FormatOptions options;

  SnapshotPrinter(llvm::raw_ostream &out, std::string_view lineStart,
                  FormatOptions options)
      : out(out), lineStart(lineStart), options(options) {}

  void printDocs(std::string_view header,
                 const google::protobuf::RepeatedPtrField<std::string> &);
  void printSymbol(std::string_view);
  void printRelationships(const scip::SymbolInformation &);

  static FormatOptions readFormatOptions(AbsolutePathRef);

public:
  static std::string
  formatExternalSymbols(std::vector<scip::SymbolInformation> &&);
  static void printDocument(const scip::Document &document,
                            AbsolutePathRef sourceFilePath,
                            llvm::raw_ostream &out);
};

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

struct CommandObjectBuilder {
  size_t index;
  RootRelativePathRef tuPathInSandbox;
  std::vector<std::string> commandLine;

  compdb::CommandObject build(const RootPath &rootInSandbox);
};

struct CompilationDatabaseBuilder {
  std::vector<CommandObjectBuilder> entries;

  llvm::json::Value toJSON(const RootPath &rootInSandbox);
};

/// Helper class to run snapshot tests recursively for all TUs
/// under a subdirectory.
class MultiTuSnapshotTest final {
  RootPath rootPath;
  struct InputOutput {
    RootRelativePath sourceFilePath;
    RootRelativePath snapshotPath;
  };

  std::vector<InputOutput> inputOutputs;

  static constexpr std::string_view externalSymbolsSnapshotPath =
      "external_symbols.snapshot.cc";

public:
  MultiTuSnapshotTest(RootPath &&,
                      absl::FunctionRef<std::optional<RootRelativePath>(
                          const RootRelativePath &)>
                          getSnapshotPath);

  using SnapshotContentsMap =
      absl::flat_hash_map<RootRelativePath, std::string>;

  /// Callback that runs a compilation command against a TU and returns
  /// a map containing the snapshot outputs for each file used
  ///
  /// Different TUs should not reuse the same headers, because this API
  /// currently doesn't handle index merging.
  using RunCompileCommandCallback = absl::FunctionRef<SnapshotContentsMap(
      const RootPath &rootInSandbox, CommandObjectBuilder &&)>;

  void run(SnapshotMode, RunCompileCommandCallback);

  struct MergeResult {
    SnapshotContentsMap snapshots;
    std::vector<scip::SymbolInformation> externalSymbols;
  };

  using RunMultiTuCompileCommandCallback = absl::FunctionRef<MergeResult(
      const RootPath &rootInSandbox, CompilationDatabaseBuilder &&)>;

  void runWithMerging(SnapshotMode, RunMultiTuCompileCommandCallback);

private:
  using InputToOutputMap =
      absl::flat_hash_map<RootRelativePathRef, RootRelativePathRef>;
  InputToOutputMap buildInputToOutputMap();

  using PerTuCallback = absl::FunctionRef<void(CommandObjectBuilder &&)>;
  void iterateOverTus(PerTuCallback);

  void checkOrUpdate(SnapshotMode, SnapshotContentsMap &&,
                     const InputToOutputMap &);
};

} // namespace test
} // namespace scip_clang

#endif // SCIP_CLANG_TEST_SNAPSHOT_H