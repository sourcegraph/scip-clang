#ifndef SCIP_CLANG_DRIVER_WORKER_COMMS_H
#define SCIP_CLANG_DRIVER_WORKER_COMMS_H

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/JSON.h"

#include "indexer/Derive.h"
#include "indexer/Hash.h"

namespace scip_clang {

using WorkerId = uint64_t;

std::string driverToWorkerQueueName(std::string_view driverId,
                                    WorkerId workerId);
std::string workerToDriverQueueName(std::string_view driverId);

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

  DERIVE_HASH_1(JobId, self._id)
  DERIVE_EQ_ALL(JobId)

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

struct HeaderInfo {
  std::string headerPath;
  HashValue hashValue;

  friend std::strong_ordering operator<=>(const HeaderInfo &lhs,
                                          const HeaderInfo &rhs);
};
SERIALIZABLE(HeaderInfo)

struct HeaderInfoMulti {
  std::string headerPath;
  std::vector<HashValue> hashValues;

  friend std::strong_ordering operator<=>(const HeaderInfoMulti &lhs,
                                          const HeaderInfoMulti &rhs);
};
SERIALIZABLE(HeaderInfoMulti)

struct SemanticAnalysisJobResult {
  std::vector<HeaderInfo> singlyExpandedHeaders;
  std::vector<HeaderInfoMulti> multiplyExpandedHeaders;

  // clang-format off
  SemanticAnalysisJobResult() = default;
  SemanticAnalysisJobResult(SemanticAnalysisJobResult &&) = default;
  SemanticAnalysisJobResult &operator=(SemanticAnalysisJobResult &&) = default;
  SemanticAnalysisJobResult(const SemanticAnalysisJobResult &) = delete;
  SemanticAnalysisJobResult &operator=(const SemanticAnalysisJobResult &) = delete;
  // clang-format on
};
SERIALIZABLE(SemanticAnalysisJobResult)

struct EmitIndexJobResult {
  std::string indexPartPath;
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
