#ifndef SCIP_CLANG_WORKER_H
#define SCIP_CLANG_WORKER_H

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "spdlog/fwd.h"

#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/CliOptions.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"

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

  // This is a static method instead of a constructor so that the
  // implicit memberwise initializer is synthesized and available
  // for test code.
  static WorkerOptions fromCliOptions(const CliOptions &);
};

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
  void performSemanticAnalysis(SemanticAnalysisJobDetails &&,
                               SemanticAnalysisJobResult &);
  void flushStreams();

private:
  const IpcOptions &ipcOptions() const;
  void processRequest(IndexJobRequest &&, IndexJobResult &);
};

} // namespace scip_clang

#endif // SCIP_CLANG_WORKER_H