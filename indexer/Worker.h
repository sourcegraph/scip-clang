#ifndef SCIP_CLANG_WORKER_H
#define SCIP_CLANG_WORKER_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "spdlog/fwd.h"

#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/CliOptions.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"

namespace scip_clang {

int workerMain(CliOptions &&);

// Type representing the driver<->worker queues, as used by a worker.
struct MessageQueuePair {
  JsonIpcQueue driverToWorker;
  JsonIpcQueue workerToDriver;

  MessageQueuePair(std::string_view driverId, WorkerId workerId);
};

struct PreprocessorHistoryRecorder {
  HeaderFilter filter;
  llvm::yaml::Output yamlStream;
};

struct WorkerOptions {
  std::chrono::seconds receiveTimeout;
  spdlog::level::level_enum logLevel;
  bool deterministic;
  std::string recordHistoryRegex;
  std::string preprocessorHistoryLogPath;
  std::string driverId;
  uint64_t workerId;

  WorkerOptions(const CliOptions &cliOptions);
};

class Worker {
  WorkerOptions options;
  MessageQueuePair messageQueues;

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
  void performSemanticAnalysis(SemanticAnalysisJobDetails &&job,
                               SemanticAnalysisJobResult &result);
  void flushStreams();

private:
  void processRequest(IndexJobRequest &&request, IndexJobResult &result);
};

} // namespace scip_clang

#endif // SCIP_CLANG_WORKER_H