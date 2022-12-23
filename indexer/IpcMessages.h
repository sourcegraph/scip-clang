#ifndef SCIP_CLANG_DRIVER_WORKER_COMMS_H
#define SCIP_CLANG_DRIVER_WORKER_COMMS_H

#include <chrono>
#include <cstdint>
#include <variant>

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/JSON.h"

namespace scip_clang {

using WorkerId = uint64_t;

std::string driverToWorkerQueueName(std::string_view driverId,
                                    WorkerId workerId);
std::string workerToDriverQueueName(std::string_view driverId);

#define SERIALIZABLE(T)                \
  llvm::json::Value toJSON(const T &); \
  bool fromJSON(const llvm::json::Value &value, T &, llvm::json::Path path);

class JobId {
  uint64_t _id;

  constexpr static uint64_t SHUTDOWN_VALUE = UINT64_MAX;

public:
  JobId() : _id(SHUTDOWN_VALUE) {}
  JobId(JobId &&) = default;
  JobId &operator=(JobId &&) = default;
  JobId(const JobId &) = default;
  JobId &operator=(const JobId &) = default;
  JobId(uint64_t id) : _id(id) {}

  template <typename H> friend H AbslHashValue(H h, const JobId &x) {
    return H::combine(std::move(h), x.id());
  }

  bool operator==(const JobId &other) const {
    return this->id() == other.id();
  }

  uint64_t id() const {
    return this->_id;
  }

  static const JobId Shutdown() {
    return JobId(SHUTDOWN_VALUE);
  }
};
SERIALIZABLE(JobId)

struct SemanticAnalysisJobDetails {
  clang::tooling::CompileCommand command;
};
SERIALIZABLE(SemanticAnalysisJobDetails)

struct EmitIndexJobDetails {
  std::vector<std::string> headersToBeEmitted;
  std::string outputDirectory;
};
SERIALIZABLE(EmitIndexJobDetails)

// NOTE(def: avoiding-unions):
// I'm avoiding using tagged unions because writing constructors
// and destructors is cumbersome (due to present of std::string in fields).
// I'm avoiding using std::variant because the API doesn't allow switching
// over a tag.

struct IndexJob {
  // See also NOTE(ref: avoiding-unions)

  enum class Kind {
    SemanticAnalysis,
    EmitIndex,
  } kind;
  SemanticAnalysisJobDetails semanticAnalysis;
  EmitIndexJobDetails emitIndex;
};
SERIALIZABLE(IndexJob::Kind)
SERIALIZABLE(IndexJob)

struct IndexJobRequest {
  JobId id;
  IndexJob job;
};
SERIALIZABLE(IndexJobRequest)

struct Sha256Hash {
  std::array<uint8_t, 256 / 8> value;
};
SERIALIZABLE(Sha256Hash)

struct HeaderInfo {
  std::string headerPath;
  Sha256Hash hashValue;
};
SERIALIZABLE(HeaderInfo)

struct SemanticAnalysisJobResult {
  std::vector<HeaderInfo> headersProcessed;
};
SERIALIZABLE(SemanticAnalysisJobResult)

struct EmitIndexJobResult {
  std::string indexPartPath;
};
SERIALIZABLE(EmitIndexJobResult)

struct IndexJobResult {
  // See also: NOTE(ref: avoiding-unions)

  IndexJob::Kind kind;

  SemanticAnalysisJobResult semanticAnalysis;
  EmitIndexJobResult emitIndex;
};
SERIALIZABLE(IndexJobResult)

struct IndexJobResponse {
  WorkerId workerId;
  JobId jobId;
  IndexJobResult result;
};
SERIALIZABLE(IndexJobResponse)

#undef SERIALIZABLE

} // namespace scip_clang

#endif // SCIP_CLANG_DRIVER_WORKER_COMMS_H
