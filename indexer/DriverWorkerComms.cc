#include "spdlog/fmt/fmt.h"

#include "llvm/Support/JSON.h"

#include "indexer/DriverWorkerComms.h"

namespace scip_clang {

std::string driverToWorkerQueueName(std::string_view driverId,
                                    WorkerId workerId) {
  return fmt::format("scip-clang-{}-worker-{}-recv", driverId, workerId);
}

std::string workerToDriverQueueName(std::string_view driverId) {
  return fmt::format("scip-clang-{}-worker-send", driverId);
}

llvm::json::Value toJSON(const JobRequest &r) {
  return llvm::json::Object{{"id", r.id.id()}, {"value", r.job.value()}};
}

bool fromJSON(const llvm::json::Value &jsonValue, JobRequest &r,
              llvm::json::Path path) {
  llvm::json::ObjectMapper mapper(jsonValue, path);
  uint64_t id;
  int value;
  bool ret = mapper && mapper.map("id", id) && mapper.map("value", value);
  if (ret) {
    r = JobRequest{.id = JobId(id), .job = Job(value)};
  }
  return ret;
}

llvm::json::Value toJSON(const JobResult &r) {
  return llvm::json::Object{
      {"workerId", r.workerId}, {"value", r.value}, {"jobId", r.jobId.id()}};
}

bool fromJSON(const llvm::json::Value &value, JobResult &r,
              llvm::json::Path path) {
  llvm::json::ObjectMapper mapper(value, path);
  uint64_t jobId;
  bool ret = mapper && mapper.map("workerId", r.workerId)
             && mapper.map("value", r.value) && mapper.map("jobId", jobId);
  if (ret) {
    r.jobId = JobId(jobId);
  }
  return ret;
}

} // end namespace scip_clang