#ifndef SCIP_CLANG_WORKER_H
#define SCIP_CLANG_WORKER_H

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "spdlog/fwd.h"

#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/CliOptions.h"
#include "indexer/FileSystem.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"

namespace scip {
class Index;
}

namespace scip_clang {

int workerMain(CliOptions &&);

struct PreprocessorHistoryRecorder {
  HeaderFilter filter;
  llvm::yaml::Output yamlStream;
  std::function<llvm::StringRef(llvm::StringRef)> normalizePath;
};

struct PreprocessorHistoryRecordingOptions {
  std::string filterRegex;
  std::string preprocessorHistoryLogPath;
  bool preferRelativePaths;
  std::string rootPath;
};

struct WorkerOptions {
  IpcOptions ipcOptions;
  spdlog::level::level_enum logLevel;
  bool deterministic;
  PreprocessorHistoryRecordingOptions recordingOptions;
  StdPath temporaryOutputDir;
  std::string workerFault;

  // This is a static method instead of a constructor so that the
  // implicit memberwise initializer is synthesized and available
  // for test code.
  static WorkerOptions fromCliOptions(const CliOptions &);
};

/// Callback passed into the AST consumer so that it can decide
/// which headers to index when traversing the translation unit.
///
/// The return value is true iff the indexing job should be run.
using WorkerCallback = absl::FunctionRef<bool(SemanticAnalysisJobResult &&,
                                              EmitIndexJobDetails &)>;

class Worker final {
  WorkerOptions options;
  // Non-null in actual builds, null in testing.
  std::unique_ptr<MessageQueuePair> messageQueues;

  /// The llvm::yaml::Output object doesn't take ownership
  /// of the underlying stream, so hold it separately.
  ///
  /// The stream is wrapped in an extra unique_ptr because
  /// \c llvm::raw_fd_ostream doesn't have a move constructor
  /// for some reason.
  std::optional<std::pair<std::unique_ptr<llvm::raw_fd_ostream>,
                          PreprocessorHistoryRecorder>>
      recorder;

public:
  Worker(WorkerOptions &&options);
  void run();

private:
  const IpcOptions &ipcOptions() const;

  enum class ReceiveStatus {
    DriverTimeout,
    MalformedMessage,
    Shutdown,
    OK,
  };

  ReceiveStatus waitForRequest(IndexJobRequest &);
  void sendResult(JobId, IndexJobResult &&);

  ReceiveStatus
  processTranslationUnitAndRespond(IndexJobRequest &&semanticAnalysisRequest);
  void emitIndex(scip::Index &&scipIndex, const StdPath &outputPath);

  ReceiveStatus processRequest(IndexJobRequest &&, IndexJobResult &);
  void triggerFaultIfApplicable() const;

  // Testing-only APIs
public:
  void processTranslationUnit(SemanticAnalysisJobDetails &&, WorkerCallback,
                              scip::Index &);
  void flushStreams();
};

} // namespace scip_clang

#endif // SCIP_CLANG_WORKER_H