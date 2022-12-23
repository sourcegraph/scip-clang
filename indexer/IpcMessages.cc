#include <type_traits>

#include "spdlog/fmt/fmt.h"

#include "llvm/Support/JSON.h"

#include "indexer/IpcMessages.h"

namespace scip_clang {

std::string driverToWorkerQueueName(std::string_view driverId,
                                    WorkerId workerId) {
  return fmt::format("scip-clang-{}-worker-{}-recv", driverId, workerId);
}

std::string workerToDriverQueueName(std::string_view driverId) {
  return fmt::format("scip-clang-{}-worker-send", driverId);
}

llvm::json::Value toJSON(const JobId &jobId) {
  return llvm::json::Value(jobId.id());
}
bool fromJSON(const llvm::json::Value &value, JobId &jobId,
              llvm::json::Path path) {
  if (auto uint = value.getAsUINT64()) {
    jobId = JobId(uint.value());
    return true;
  }
  path.report("expected uint64_t for job");
  return false;
}

llvm::json::Value toJSON(const IndexJob::Kind &kind) {
  switch (kind) {
  case IndexJob::Kind::SemanticAnalysis:
    return llvm::json::Value("SemanticAnalysis");
  case IndexJob::Kind::EmitIndex:
    return llvm::json::Value("EmitIndex");
  }
}

bool fromJSON(const llvm::json::Value &jsonValue, IndexJob::Kind &t,
              llvm::json::Path path) {
  if (auto s = jsonValue.getAsString()) {
    if (s.value() == "SemanticAnalysis") {
      t = IndexJob::Kind::SemanticAnalysis;
      return true;
    } else if (s.value() == "EmitIndex") {
      t = IndexJob::Kind::EmitIndex;
      return true;
    }
  }
  path.report("expected SemanticAnalysis or EmitIndex for IndexJob::Kind");
  return false;
}

template <typename IJ> llvm::json::Value toJSONIndexJob(const IJ &job) {
  llvm::json::Value details("");
  switch (job.kind) {
  case IndexJob::Kind::SemanticAnalysis:
    details = toJSON(job.semanticAnalysis);
    break;
  case IndexJob::Kind::EmitIndex:
    details = toJSON(job.emitIndex);
    break;
  }
  return llvm::json::Object{{"kind", toJSON(job.kind)}, {"details", details}};
}
template <typename IJ>
bool fromJSONIndexJob(const llvm::json::Value &jsonValue, IJ &t,
                      llvm::json::Path path) {
  llvm::json::ObjectMapper mapper(jsonValue, path);
  bool ret = mapper && mapper.map("kind", t.kind);
  if (ret) {
    switch (t.kind) {
    case IndexJob::Kind::SemanticAnalysis:
      ret = mapper.map("details", t.semanticAnalysis);
      break;
    case IndexJob::Kind::EmitIndex:
      ret = mapper.map("details", t.emitIndex);
      break;
    }
  }
  return ret;
}

llvm::json::Value toJSON(const IndexJob &job) {
  return toJSONIndexJob(job);
}
bool fromJSON(const llvm::json::Value &jsonValue, IndexJob &job,
              llvm::json::Path path) {
  return fromJSONIndexJob(jsonValue, job, path);
}
llvm::json::Value toJSON(const IndexJobResult &job) {
  return toJSONIndexJob(job);
}
bool fromJSON(const llvm::json::Value &jsonValue, IndexJobResult &job,
              llvm::json::Path path) {
  return fromJSONIndexJob(jsonValue, job, path);
}

llvm::json::Value toJSON(const Sha256Hash &h) {
  return llvm::json::Array(h.value);
}
bool fromJSON(const llvm::json::Value &jsonValue, Sha256Hash &h,
              llvm::json::Path path) {
  if (auto v = jsonValue.getAsArray()) {
    if (v->size() == sizeof(decltype(h.value))) {
      for (size_t i = 0; i < v->size(); i++) {
        if (auto u = (*v)[i].getAsUINT64()) {
          if (u.value() <= uint64_t(UINT8_MAX)) {
            h.value[i] = u.value();
            continue;
          }
        }
        path.report("expected uint8 in array");
        return false;
      }
      return true;
    }
    path.report("expected array with size 32");
    return false;
  }
  path.report("expected array<uint8_t> for header hash");
  return false;
}

#define DERIVE_SERIALIZE(T, F1)                           \
  llvm::json::Value toJSON(const T &t) {                  \
    return llvm::json::Object{                            \
        {#F1, t.F1},                                      \
    };                                                    \
  }                                                       \
  bool fromJSON(const llvm::json::Value &jsonValue, T &t, \
                llvm::json::Path path) {                  \
    llvm::json::ObjectMapper mapper(jsonValue, path);     \
    return mapper && mapper.map(#F1, t.F1);               \
  }
DERIVE_SERIALIZE(scip_clang::SemanticAnalysisJobResult, headersProcessed)
DERIVE_SERIALIZE(scip_clang::EmitIndexJobResult, indexPartPath)
#undef DERIVE_SERIALIZE

#define DERIVE_SERIALIZE(T, F1, F2)                                  \
  llvm::json::Value toJSON(const T &t) {                             \
    return llvm::json::Object{                                       \
        {#F1, t.F1},                                                 \
        {#F2, t.F2},                                                 \
    };                                                               \
  }                                                                  \
  bool fromJSON(const llvm::json::Value &jsonValue, T &t,            \
                llvm::json::Path path) {                             \
    llvm::json::ObjectMapper mapper(jsonValue, path);                \
    return mapper && mapper.map(#F1, t.F1) && mapper.map(#F2, t.F2); \
  }

DERIVE_SERIALIZE(scip_clang::HeaderInfo, headerPath, hashValue)
DERIVE_SERIALIZE(scip_clang::EmitIndexJobDetails, headersToBeEmitted,
                 outputDirectory)
DERIVE_SERIALIZE(scip_clang::IndexJobRequest, id, job)

llvm::json::Value toJSON(const SemanticAnalysisJobDetails &val) {
  return llvm::json::Object{{"workdir", val.command.Directory},
                            {"file", val.command.Filename},
                            {"output", val.command.Output},
                            {"args", val.command.CommandLine}};
}

bool fromJSON(const llvm::json::Value &jsonValue, SemanticAnalysisJobDetails &d,
              llvm::json::Path path) {
  llvm::json::ObjectMapper mapper(jsonValue, path);
  return mapper && mapper.map("workdir", d.command.Directory)
         && mapper.map("file", d.command.Filename)
         && mapper.map("output", d.command.Output)
         && mapper.map("args", d.command.CommandLine);
}

llvm::json::Value toJSON(const IndexJobResponse &r) {
  return llvm::json::Object{
      {"workerId", r.workerId}, {"jobId", r.jobId}, {"result", r.result}};
}

bool fromJSON(const llvm::json::Value &value, IndexJobResponse &r,
              llvm::json::Path path) {
  llvm::json::ObjectMapper mapper(value, path);
  return mapper && mapper.map("workerId", r.workerId)
         && mapper.map("jobId", r.jobId) && mapper.map("result", r.result);
}

} // namespace scip_clang