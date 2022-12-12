#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

#include "indexer/Driver.h"
#include "indexer/DriverWorkerComms.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LLVMAdapter.h"
#include "indexer/Logging.h"

namespace boost_ip = boost::interprocess;

namespace scip_clang {

// Type representing the driver<->worker queues, as used by the driver;
struct MessageQueues {
  std::vector<JsonIpcQueue> driverToWorker;
  JsonIpcQueue workerToDriver;

  MessageQueues() : driverToWorker(), workerToDriver() {}

  static void deleteIfPresent(std::string_view driverId, size_t numWorkers) {
    for (WorkerId workerId = 0; workerId < numWorkers; workerId++) {
      auto d2w = scip_clang::driverToWorkerQueueName(driverId, workerId);
      boost_ip::message_queue::remove(d2w.c_str());
    }
    auto w2d = scip_clang::workerToDriverQueueName(driverId);
    boost_ip::message_queue::remove(w2d.c_str());
  }

  MessageQueues(std::string_view driverId, size_t numWorkers,
                std::pair<size_t, size_t> elementSizes) {
    for (WorkerId workerId = 0; workerId < numWorkers; workerId++) {
      auto d2w = scip_clang::driverToWorkerQueueName(driverId, workerId);
      driverToWorker.emplace_back(
          JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
              boost_ip::create_only, d2w.c_str(), 1, elementSizes.first)));
    }
    auto w2d = scip_clang::workerToDriverQueueName(driverId);
    this->workerToDriver =
        JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
            boost_ip::create_only, w2d.c_str(), numWorkers,
            elementSizes.second));
  }
};

struct WorkerInfo {
  boost::process::child processHandle;

  enum class Status {
    Busy,
    Free,
  } status;

  // Used when status == Busy
  std::chrono::time_point<std::chrono::steady_clock> startedJob;
  // Non-null when status == Busy
  std::optional<JobId> currentlyProcessing;
};

class Driver {
  std::string id;
  std::string workerExecutablePath;

  size_t numWorkers;
  std::vector<WorkerInfo> workers;
  std::deque<unsigned> availableWorkers; // Keep track of workers which are
                                         // available in FIFO order.

  MessageQueues queues;

  uint64_t nextJobId = 1;
  absl::flat_hash_map<JobId, Job> allJobList;
  std::deque<JobId> pendingJobs;
  absl::flat_hash_set<JobId> wipJobs;

public:
  Driver(size_t numWorkers, const char *workerExecutablePath)
      : id(fmt::format("{}", ::getpid())),
        workerExecutablePath(workerExecutablePath), numWorkers(numWorkers) {
    MessageQueues::deleteIfPresent(this->id, numWorkers);
    this->queues = MessageQueues(this->id, numWorkers,
                                 {IPC_BUFFER_MAX_SIZE, IPC_BUFFER_MAX_SIZE});
  }

  void spawnWorkers() {
    this->workers.resize(numWorkers);
    for (unsigned i = 0; i < numWorkers; i++) {
      this->spawnWorker(i);
    }
  }

  void queueJob(Job &&j) {
    auto jobId = JobId(this->nextJobId);
    this->nextJobId++;
    this->allJobList.insert({jobId, j});
    this->pendingJobs.push_back(jobId);
  }

  void runJobsTillCompletionAndShutdownWorkers() {
    while (!this->pendingJobs.empty() || !this->wipJobs.empty()) {
      if (this->pendingJobs.empty()) {
        this->processOneJobResult();
        // There are a few cases here:
        // 1. We finished processing some responses.
        //    In this case, there may be more jobs or workers available.
        //    Continue with the loop.
        // 2. We didn't get any responses.
      } else if (this->availableWorkers.empty()) {
        this->processOneJobResult();
      } else {
        this->assignJobsToAvailableWorkers();
        this->processOneJobResult();
      }
    }
    this->shutdownAllWorkers();
    this->waitForAllWorkers();
    // Send shutdown messages to all the workers.
    // Now wait for each of them to shutdown.
  }

private:
  void spawnWorker(WorkerId workerId) {
    std::vector<std::string> args;
    args.push_back(std::string(workerExecutablePath));
    args.push_back("worker");
    args.push_back("--driver-id");
    args.push_back(this->id);
    args.push_back("--worker-id");
    args.push_back(fmt::format("{}", workerId));
    boost::process::child worker(args, boost::process::std_out > stdout);
    spdlog::info("worker info running {}, pid = {}", worker.running(),
                 worker.id());
    this->workers[workerId] = WorkerInfo{.processHandle = std::move(worker),
                                         .status = WorkerInfo::Status::Free};
    this->availableWorkers.push_back(workerId);
  }

  void killLongRunningWorkersAndRespawn(
      std::chrono::time_point<std::chrono::steady_clock> startedBefore) {
    for (unsigned i = 0; i < this->workers.size(); ++i) {
      auto &workerInfo = this->workers[i];
      switch (workerInfo.status) {
      case WorkerInfo::Status::Free:
        continue;
      case WorkerInfo::Status::Busy:
        if (workerInfo.startedJob < startedBefore) {
          spdlog::info("killing worker {}, pid {}", i,
                       workerInfo.processHandle.id());
          workerInfo.processHandle.terminate();
          this->spawnWorker(i);
        }
      }
    }
  }

  void markWorkerFree(WorkerId workerId) {
    auto &workerInfo = this->workers[workerId];
    workerInfo.currentlyProcessing = {};
    workerInfo.status = WorkerInfo::Status::Free;
    this->availableWorkers.push_front(workerId);
  }

  void markWorkerBusy(WorkerId workerId, JobId jobId) {
    auto &nextWorkerInfo = this->workers[workerId];
    nextWorkerInfo.status = WorkerInfo::Status::Busy;
    nextWorkerInfo.currentlyProcessing = {jobId};
    nextWorkerInfo.startedJob = std::chrono::steady_clock::now();
  }

  void processOneJobResult() {
    using namespace std::chrono_literals;
    // TODO: Make this timeout configurable
    auto workerTimeoutLimit = 2s;
    // NOTE: Wait duration has to be longer than the timeout.
    auto waitDuration = workerTimeoutLimit + 1s;

    JobResult result;
    auto recvError =
        this->queues.workerToDriver.timedReceive(result, waitDuration);
    if (recvError.isA<TimeoutError>()) {
      // All workers which are working have been doing so for too long,
      // because waitDuration is always longer than the timeout.
      auto now = std::chrono::steady_clock::now();
      this->killLongRunningWorkersAndRespawn(now - workerTimeoutLimit);
    } else if (recvError) {
      spdlog::error("received malformed message: {}",
                    scip_clang::formatLLVM(recvError));
      // Log the error and continue
    } else {
      spdlog::info("received {} from worker {}", result.value, result.workerId);

      this->markWorkerFree(result.workerId);
      bool erased = wipJobs.erase(result.jobId);
      assert(erased);

      auto now = std::chrono::steady_clock::now();
      this->killLongRunningWorkersAndRespawn(now - workerTimeoutLimit);
    }
  }

  void assignJobToWorker(WorkerId workerId, JobId jobId) {
    this->wipJobs.insert(jobId);
    this->markWorkerBusy(workerId, jobId);
    auto it = this->allJobList.find(jobId);
    assert(it != this->allJobList.end());
    this->queues.driverToWorker[workerId].send(
        JobRequest{it->first, it->second});
  }

  void assignJobsToAvailableWorkers() {
    auto numJobsToAssign =
        std::min(this->availableWorkers.size(), this->pendingJobs.size());
    assert(numJobsToAssign >= 1);
    for (unsigned i = 0; i < numJobsToAssign; ++i) {
      JobId nextJob = this->pendingJobs.front();
      this->pendingJobs.pop_front();
      unsigned nextWorkerId = this->availableWorkers.front();
      this->availableWorkers.pop_front();
      spdlog::info("assigning jobId {} (data: {}) to worker {}", nextJob.id(),
                   this->allJobList[nextJob].value(), nextWorkerId);
      this->assignJobToWorker(nextWorkerId, nextJob);
    }
  }

  void shutdownAllWorkers() {
    assert(this->availableWorkers.size() == this->numWorkers);
    for (unsigned i = 0; i < numWorkers; ++i) {
      this->queues.driverToWorker[i].send(JobRequest{JobId::Shutdown(), 0});
    }
  }

  void waitForAllWorkers() {
    for (auto &worker : this->workers) {
      worker.processHandle.wait();
    }
  }
};

int driverMain(int argc, char *argv[]) {
  pid_t driverPid = ::getpid();
  auto driverId = fmt::format("{}", driverPid);

  scip_clang::initialize_global_logger("driver");

  // TODO: Take an optional jobs argument that defaults to number of threads.
  size_t numWorkers = 2;

  BOOST_TRY {
    MessageQueues::deleteIfPresent(driverId, numWorkers);

    Driver driver(numWorkers, argv[0]);
    driver.spawnWorkers();

    for (int i = 0; i < 5; i++) {
      driver.queueJob(Job(i));
    }
    driver.runJobsTillCompletionAndShutdownWorkers();
    spdlog::info("worker(s) exited; going now kthxbai");
  }
  BOOST_CATCH(boost_ip::interprocess_exception & ex) {
    spdlog::error("driver caught exception {}", ex.what());
    return 1;
  }
  BOOST_CATCH_END
  MessageQueues::deleteIfPresent(driverId, numWorkers);
  return 0;
}

} // end namespace scip_clang