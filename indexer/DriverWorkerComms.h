#ifndef SCIP_CLANG_DRIVER_WORKER_COMMS_H
#define SCIP_CLANG_DRIVER_WORKER_COMMS_H

#include <chrono>
#include <cstdint>

#include "llvm/Support/JSON.h"

namespace scip_clang {

using WorkerId = unsigned long long;

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

class Job {
  int _value;

public:
  Job() = default;
  Job(Job &&) = default;
  Job &operator=(Job &&) = default;
  Job(const Job &) = default;
  Job &operator=(const Job &) = default;
  Job(int val) : _value(val) {}

  int value() const {
    return this->_value;
  }
};

struct JobRequest {
  JobId id;
  Job job;
};

llvm::json::Value toJSON(const JobRequest &job);

bool fromJSON(const llvm::json::Value &value, JobRequest &j,
              llvm::json::Path path);

struct JobResult {
  WorkerId workerId;
  JobId jobId;
  int value;
};

llvm::json::Value toJSON(const JobResult &r);
bool fromJSON(const llvm::json::Value &value, JobResult &r,
              llvm::json::Path path);

} // end namespace scip_clang

#endif // SCIP_CLANG_DRIVER_WORKER_COMMS_H