#ifndef SCIP_CLANG_DRIVER_WORKER_COMMS_H
#define SCIP_CLANG_DRIVER_WORKER_COMMS_H

#include <chrono>
#include <cstdint>
#include <cstring>
#include <variant>

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Regex.h"

#include "indexer/Hash.h"

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

class HeaderFilter final {
  // The original text of the regex, because llvm::Regex doesn't expose
  // an API for serializing to a string.
  std::string _regexText;
  std::optional<llvm::Regex> matcher;

public:
  HeaderFilter() = default;
  HeaderFilter(HeaderFilter &&) = default;
  HeaderFilter &operator=(HeaderFilter &&) = default;

  HeaderFilter(std::string &&regexText);

  bool isMatch(std::string_view data) const {
    if (matcher && matcher->match(llvm::StringRef(data))) {
      return true;
    }
    return false;
  }

  bool isIdentity() const {
    return this->regexText().empty();
  }

  std::string regexText() const {
    return this->_regexText;
  }
};
SERIALIZABLE(HeaderFilter)

struct SemanticAnalysisJobDetails {
  clang::tooling::CompileCommand command;
  HeaderFilter recordHistoryFilter;
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

  friend bool operator<(const HeaderInfo &lhs, const HeaderInfo &rhs) {
    if (lhs.hashValue < rhs.hashValue) {
      return true;
    }
    if (lhs.hashValue == rhs.hashValue) {
      return lhs.headerPath < rhs.headerPath;
    }
    return false;
  }
};
SERIALIZABLE(HeaderInfo)

struct HeaderInfoMulti {
  std::string headerPath;
  std::vector<HashValue> hashValues;

  friend bool operator<(const HeaderInfoMulti &lhs,
                        const HeaderInfoMulti &rhs) {
    auto cmp = std::strcmp(lhs.headerPath.c_str(), rhs.headerPath.c_str());
    if (cmp < 0) {
      return true;
    }
    if (cmp == 0) {
      return lhs.hashValues < rhs.hashValues;
    }
    return false;
  }
};
SERIALIZABLE(HeaderInfoMulti)

struct SemanticAnalysisJobResult {
  std::vector<HeaderInfo> singlyExpandedHeaders;
  std::vector<HeaderInfoMulti> multiplyExpandedHeaders;

  SemanticAnalysisJobResult() = default;
  SemanticAnalysisJobResult(SemanticAnalysisJobResult &&) = default;
  SemanticAnalysisJobResult &operator=(SemanticAnalysisJobResult &&) = default;
  SemanticAnalysisJobResult(const SemanticAnalysisJobResult &) = delete;
  SemanticAnalysisJobResult &
  operator=(const SemanticAnalysisJobResult &) = delete;
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

#undef SERIALIZABLE

} // namespace scip_clang

#endif // SCIP_CLANG_DRIVER_WORKER_COMMS_H
