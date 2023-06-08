#ifndef SCIP_CLANG_DRIVER_WORKER_COMMS_H
#define SCIP_CLANG_DRIVER_WORKER_COMMS_H

#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "llvm/Support/JSON.h"

#include "indexer/CompilationDatabase.h"
#include "indexer/Derive.h"
#include "indexer/Hash.h"
#include "indexer/Path.h"

namespace scip_clang {

using WorkerId = uint64_t;

std::string driverToWorkerQueueName(std::string_view driverId,
                                    WorkerId workerId);
std::string workerToDriverQueueName(std::string_view driverId);

class JobId {
  // Corresponds 1-1 with an entry in a compilation database.
  uint32_t _taskId;
  uint32_t subtaskId;

  constexpr static uint32_t SHUTDOWN_VALUE = UINT32_MAX;
  JobId(uint32_t taskId, uint32_t subtaskId)
      : _taskId(taskId), subtaskId(subtaskId) {}

public:
  JobId() : _taskId(SHUTDOWN_VALUE), subtaskId(SHUTDOWN_VALUE) {}
  JobId(JobId &&) = default;
  JobId &operator=(JobId &&) = default;
  JobId(const JobId &) = default;
  JobId &operator=(const JobId &) = default;
  static JobId newTask(uint32_t taskId) {
    return JobId{taskId, 0};
  }
  JobId nextSubtask() const {
    return JobId(this->_taskId, this->subtaskId + 1);
  }

  uint32_t taskId() const {
    return this->_taskId;
  }

  uint64_t traceId() const {
    return this->to64Bit();
  }

private:
  uint64_t to64Bit() const {
    return (uint64_t(this->_taskId) << 32) + uint64_t(this->subtaskId);
  }

  static JobId from64Bit(uint64_t v) {
    return JobId(v >> 32, static_cast<uint32_t>(v));
  }

public:
  friend fmt::formatter<scip_clang::JobId>;

  DERIVE_HASH_1(JobId, self.to64Bit())
  DERIVE_EQ_ALL(JobId)

  static const JobId Shutdown() {
    return JobId();
  }

  static llvm::json::Value toJSON(const JobId &);
  static bool fromJSON(const llvm::json::Value &, JobId &, llvm::json::Path);
};
SERIALIZABLE(JobId)

} // namespace scip_clang

template <> struct fmt::formatter<scip_clang::JobId> {
  constexpr auto parse(fmt::format_parse_context &ctx)
      -> decltype(ctx.begin()) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it != '}')
      throw fmt::format_error("unexpected format specifier for JobID");
    return it;
  }

  template <typename FormatContext>
  auto format(const scip_clang::JobId &jobId, FormatContext &ctx) const
      -> decltype(ctx.out()) {
    return fmt::format_to(
        ctx.out(), "(compdb index: {}, subtask: {})", jobId.taskId(),
        jobId.subtaskId == 0 ? "semantic analysis" : "emit index");
  }
};

namespace scip_clang {

struct SemanticAnalysisJobDetails {
  compdb::CommandObject command;
};
SERIALIZABLE(SemanticAnalysisJobDetails)

} // namespace scip_clang

namespace clang::tooling {
SERIALIZABLE(clang::tooling::CompileCommand) // Not implemented upstream 😕
}

namespace scip_clang {

struct PreprocessedFileInfo {
  AbsolutePath path;
  HashValue hashValue;

  friend std::strong_ordering operator<=>(const PreprocessedFileInfo &lhs,
                                          const PreprocessedFileInfo &rhs);
};
SERIALIZABLE(PreprocessedFileInfo)

struct PreprocessedFileInfoMulti {
  AbsolutePath path;
  std::vector<HashValue> hashValues;

  friend std::strong_ordering operator<=>(const PreprocessedFileInfoMulti &lhs,
                                          const PreprocessedFileInfoMulti &rhs);
};
SERIALIZABLE(PreprocessedFileInfoMulti)

struct EmitIndexJobDetails {
  std::vector<PreprocessedFileInfo> filesToBeIndexed;
};
SERIALIZABLE(EmitIndexJobDetails)

// NOTE(def: avoiding-unions):
// I'm avoiding using tagged unions because writing constructors
// and destructors is cumbersome (due to present of std::string in fields).
// I'm avoiding using std::variant because the API doesn't allow switching
// over a tag.

struct IndexJob {
  enum class Kind {
    SemanticAnalysis,
    EmitIndex,
  } kind;
  SemanticAnalysisJobDetails semanticAnalysis;
  EmitIndexJobDetails emitIndex;

  // See also NOTE(ref: avoiding-unions)
};
SERIALIZABLE(IndexJob::Kind)
SERIALIZABLE(IndexJob)

struct IndexJobRequest {
  JobId id;
  IndexJob job;
};
SERIALIZABLE(IndexJobRequest)

SERIALIZABLE(HashValue)

struct SemanticAnalysisJobResult {
  std::vector<PreprocessedFileInfo> wellBehavedFiles;
  std::vector<PreprocessedFileInfoMulti> illBehavedFiles;

  // clang-format off
  SemanticAnalysisJobResult() = default;
  SemanticAnalysisJobResult(SemanticAnalysisJobResult &&) = default;
  SemanticAnalysisJobResult &operator=(SemanticAnalysisJobResult &&) = default;
  SemanticAnalysisJobResult(const SemanticAnalysisJobResult &) = delete;
  SemanticAnalysisJobResult &operator=(const SemanticAnalysisJobResult &) = delete;
  // clang-format on
};
SERIALIZABLE(SemanticAnalysisJobResult)

struct IndexingStatistics {
  uint64_t totalTimeMicros;
};
SERIALIZABLE(IndexingStatistics)

struct ShardPaths {
  AbsolutePath docsAndExternals;
  AbsolutePath forwardDecls;

  static std::string prefix(uint32_t taskId, WorkerId workerId);

  static std::optional<uint32_t> tryParseJobId(std::string_view fileName);
};
SERIALIZABLE(ShardPaths)

struct EmitIndexJobResult {
  IndexingStatistics statistics;
  ShardPaths shardPaths;
};
SERIALIZABLE(EmitIndexJobResult)

struct IndexJobResult {
  IndexJob::Kind kind;
  SemanticAnalysisJobResult semanticAnalysis;
  EmitIndexJobResult emitIndex;
  // See also: NOTE(ref: avoiding-unions)
};
SERIALIZABLE(IndexJobResult)

struct IndexJobResponse {
  WorkerId workerId;
  JobId jobId;
  IndexJobResult result;
};
SERIALIZABLE(IndexJobResponse)

struct IpcTestMessage {
  std::string content;
};
SERIALIZABLE(IpcTestMessage)

} // namespace scip_clang

#endif // SCIP_CLANG_DRIVER_WORKER_COMMS_H
