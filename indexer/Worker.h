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

#include "indexer/AstConsumer.h"
#include "indexer/CliOptions.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/FileSystem.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/PackageMap.h"
#include "indexer/Path.h"
#include "indexer/Preprocessing.h"

namespace google::protobuf {
class Message;
} // namespace google::protobuf

namespace scip_clang {

int workerMain(CliOptions &&);

struct PreprocessorHistoryRecordingOptions {
  std::string filterRegex;
  std::string preprocessorHistoryLogPath;
  bool preferRelativePaths;
  std::string rootPath;
};

enum class WorkerMode {
  /// The worker communicates with the driver over IPC (default)
  Ipc,
  /// The worker tries to process a compilation database directly (dev-only).
  Compdb,
  /// The worker will have methods called by testing code.
  Testing,
};

struct WorkerOptions {
  RootPath projectRootPath;

  WorkerMode mode;
  IpcOptions ipcOptions;   // only valid if mode == Ipc
  StdPath compdbPath;      // only valid if mode == Compdb
  StdPath indexOutputPath; // only valid if mode == Compdb
  StdPath statsFilePath;   // only valid if mode == Compdb

  StdPath packageMapPath;
  bool showCompilerDiagnostics;
  bool showProgress;

  spdlog::level::level_enum logLevel;
  bool deterministic;
  bool measureStatistics;
  PreprocessorHistoryRecordingOptions recordingOptions;
  StdPath temporaryOutputDir;
  std::string workerFault;

  // This is a static method instead of a constructor so that the
  // implicit memberwise initializer is synthesized and available
  // for test code.
  static WorkerOptions fromCliOptions(const CliOptions &);
};

class Worker final {
  WorkerOptions options;

  PackageMap packageMap;

  // Non-null iff options.mode == Ipc
  std::unique_ptr<MessageQueuePair> messageQueues;

  // Set iff options.mode == Compdb
  std::vector<compdb::CommandObject> compileCommands;
  size_t commandIndex;

  /// The llvm::yaml::Output object doesn't take ownership
  /// of the underlying stream, so hold it separately.
  ///
  /// The stream is wrapped in an extra unique_ptr because
  /// \c llvm::raw_fd_ostream doesn't have a move constructor
  /// for some reason.
  std::optional<std::pair<std::unique_ptr<llvm::raw_fd_ostream>,
                          PreprocessorHistoryRecorder>>
      recorder;

  IndexingStatistics statistics;

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

  ReceiveStatus sendRequestAndReceive(JobId semaRequestId,
                                      std::string_view tuMainFilePath,
                                      SemanticAnalysisJobResult &&,
                                      IndexJobRequest &emitIndexRequest);

  void emitIndex(google::protobuf::Message &&scipIndex,
                 const StdPath &outputPath);

  ReceiveStatus processRequest(IndexJobRequest &&, IndexJobResult &);
  void triggerFaultIfApplicable() const;

  // Testing-only APIs
public:
  void processTranslationUnit(SemanticAnalysisJobDetails &&, WorkerCallback,
                              TuIndexingOutput &);
  void flushStreams();
};

} // namespace scip_clang

#endif // SCIP_CLANG_WORKER_H