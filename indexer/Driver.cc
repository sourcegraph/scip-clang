#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

#include "indexer/CliOptions.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/Driver.h"
#include "indexer/FileSystem.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LLVMAdapter.h"
#include "indexer/Logging.h"

namespace boost_ip = boost::interprocess;

namespace scip_clang {
namespace {

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
  std::chrono::duration<uint64_t> receiveTimeout;

  uint64_t nextJobId = 0;
  absl::flat_hash_map<JobId, IndexJob> allJobList;
  std::deque<JobId> pendingJobs;
  absl::flat_hash_set<JobId> wipJobs;

  compdb::ResumableParser compdbParser;

  size_t totalJobCount = 0;

  std::vector<clang::tooling::CompileCommand> compileCommands;

public:
  Driver(const Driver &) = delete;
  Driver &operator=(const Driver &) = delete;

  Driver(size_t numWorkers, std::string workerExecutablePath,
         std::chrono::duration<uint64_t> receiveTimeout)
      : id(fmt::format("{}", ::getpid())),
        workerExecutablePath(workerExecutablePath), numWorkers(numWorkers),
        receiveTimeout(receiveTimeout), compdbParser() {
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

  size_t refillCount() const {
    return 2 * this->numWorkers;
  }

  size_t refillJobs() {
    std::vector<clang::tooling::CompileCommand> commands{};
    this->compdbParser.parseMore(commands);
    for (auto &command : commands) {
      this->queueJob(IndexJob{IndexJob::Kind::SemanticAnalysis,
                              SemanticAnalysisJobDetails{std::move(command)},
                              EmitIndexJobDetails{}});
    }
    return commands.size();
  }

  void queueJob(IndexJob &&j) {
    auto jobId = JobId(this->nextJobId);
    this->nextJobId++;
    this->allJobList.insert({jobId, j});
    this->pendingJobs.push_back(jobId);
  }

  void runJobsTillCompletionAndShutdownWorkers() {
    this->refillJobs();
    while (!this->pendingJobs.empty() || !this->wipJobs.empty()) {
      if (this->pendingJobs.empty()) {
        this->refillJobs();
        this->processOneJobResult();
      } else if (this->availableWorkers.empty()) {
        this->processOneJobResult();
      } else {
        this->assignJobsToAvailableWorkers();
        this->processOneJobResult();
      }
    }
    this->shutdownAllWorkers();
    this->waitForAllWorkers();
  }

  FileGuard openCompilationDatabase() {
    auto compdbPath =
        std::filesystem::current_path().append("compile_commands.json");

    std::error_code error;
    auto compdbFile = compdb::CompilationDatabaseFile::open(compdbPath, error);
    if (!compdbFile.file) {
      spdlog::error("failed to open compile_commands.json: {}",
                    std::strerror(errno));
      std::exit(EXIT_FAILURE);
    }
    if (error) {
      spdlog::error("failed to read file size for compile_commands.json: {}", error.message());
      std::exit(EXIT_FAILURE);
    }
    if (compdbFile.numJobs == 0) {
      spdlog::error("compile_commands.json has 0 objects in outermost array; "
                    "nothing to index");
      std::exit(EXIT_FAILURE);
    }
    this->totalJobCount = compdbFile.numJobs;
    spdlog::debug("total {} compilation jobs", this->totalJobCount);

    this->compdbParser.initialize(compdbFile, this->refillCount());
    return FileGuard(compdbFile.file);
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
    spdlog::debug("worker info running {}, pid = {}", worker.running(),
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
          auto oldJob = workerInfo.currentlyProcessing.value();
          this->wipJobs.erase(oldJob);
          spdlog::warn("skipping job {} due to worker timeout", oldJob.id());
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
    auto workerTimeout = this->receiveTimeout;

    IndexJobResponse response;
    auto recvError =
        this->queues.workerToDriver.timedReceive(response, workerTimeout);
    if (recvError.isA<TimeoutError>()) {
      // All workers which are working have been doing so for too long,
      // because TimeoutError means we already exceeded the timeout limit.
      auto now = std::chrono::steady_clock::now();
      this->killLongRunningWorkersAndRespawn(now - workerTimeout);
    } else if (recvError) {
      spdlog::error("received malformed message: {}",
                    scip_clang::formatLLVM(recvError));
      // Keep going instead of exiting early for robustness.
    } else {
      // TODO(def: add-job-debug-helper): Add a simplified debug representation
      // for printing jobs for debugging.
      spdlog::debug("received job from worker {}", response.workerId);

      this->markWorkerFree(response.workerId);
      bool erased = wipJobs.erase(response.jobId);
      // FIXME(ref: add-enforce)
      assert(erased);

      auto now = std::chrono::steady_clock::now();
      this->killLongRunningWorkersAndRespawn(now - workerTimeout);
    }
  }

  void assignJobToWorker(WorkerId workerId, JobId jobId) {
    // TODO(ref: add-job-debug-helper) Print abbreviated job data here.
    spdlog::info("assigning jobId {} to worker {}", jobId.id(), workerId);
    this->wipJobs.insert(jobId);
    this->markWorkerBusy(workerId, jobId);
    auto it = this->allJobList.find(jobId);
    // FIXME(ref: add-enforce)
    assert(it != this->allJobList.end());
    this->queues.driverToWorker[workerId].send(
        IndexJobRequest{it->first, it->second});
  }

  void assignJobsToAvailableWorkers() {
    auto numJobsToAssign =
        std::min(this->availableWorkers.size(), this->pendingJobs.size());
    // FIXME(ref: add-enforce)
    assert(numJobsToAssign >= 1);
    for (unsigned i = 0; i < numJobsToAssign; ++i) {
      JobId nextJob = this->pendingJobs.front();
      this->pendingJobs.pop_front();
      unsigned nextWorkerId = this->availableWorkers.front();
      this->availableWorkers.pop_front();
      this->assignJobToWorker(nextWorkerId, nextJob);
    }
  }

  void shutdownAllWorkers() {
    assert(this->availableWorkers.size() == this->numWorkers);
    for (unsigned i = 0; i < numWorkers; ++i) {
      this->queues.driverToWorker[i].send(
          IndexJobRequest{JobId::Shutdown(), {}});
    }
  }

  void waitForAllWorkers() {
    for (auto &worker : this->workers) {
      worker.processHandle.wait();
    }
  }
};

} // namespace

int driverMain(CliOptions &&cliOptions) {
  pid_t driverPid = ::getpid();
  auto driverId = fmt::format("{}", driverPid);
  size_t numWorkers = cliOptions.numWorkers;
  BOOST_TRY {
    MessageQueues::deleteIfPresent(driverId, numWorkers);

    Driver driver(numWorkers, cliOptions.scipClangExecutablePath,
                  cliOptions.receiveTimeout);
    auto compdbGuard = driver.openCompilationDatabase();
    driver.spawnWorkers();
    driver.runJobsTillCompletionAndShutdownWorkers();
    spdlog::debug("worker(s) exited; going now kthxbai");
  }
  BOOST_CATCH(boost_ip::interprocess_exception & ex) {
    spdlog::error("driver caught exception {}", ex.what());
    return 1;
  }
  BOOST_CATCH_END
  MessageQueues::deleteIfPresent(driverId, numWorkers);
  return 0;
}

} // namespace scip_clang