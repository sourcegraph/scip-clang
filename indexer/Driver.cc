#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "indexer/Enforce.h" // Defines ENFORCE required by rapidjson headers

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

struct DriverOptions {
  std::string workerExecutablePath;
  std::filesystem::path compdbPath;
  size_t numWorkers;
  std::chrono::seconds receiveTimeout;
  bool deterministic;
  std::string recordHistoryRegex;

  explicit DriverOptions(const CliOptions &cliOpts)
      : workerExecutablePath(cliOpts.scipClangExecutablePath),
        compdbPath(cliOpts.compdbPath), numWorkers(cliOpts.numWorkers),
        receiveTimeout(cliOpts.numWorkers),
        deterministic(cliOpts.deterministic),
        recordHistoryRegex(cliOpts.recordHistoryRegex) {}

  void addWorkerOptions(std::vector<std::string> &args) const {
    args.push_back(fmt::format(
        "--log-level={}", spdlog::level::to_string_view(spdlog::get_level())));
    static_assert(std::is_same<decltype(this->receiveTimeout),
                               std::chrono::seconds>::value);
    args.push_back(fmt::format("--receive-timeout-seconds={}",
                               this->receiveTimeout.count()));
    if (this->deterministic) {
      args.push_back("--deterministic");
    }
    if (!this->recordHistoryRegex.empty()) {
      args.push_back(
          fmt::format("--record-history={}", this->recordHistoryRegex));
    }
  }
};

class Driver {
  DriverOptions options;

  std::string id;

  std::vector<WorkerInfo> workers;
  std::deque<unsigned> availableWorkers; // Keep track of workers which are
                                         // available in FIFO order.

  MessageQueues queues;

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

  Driver(DriverOptions &&options)
      : options(std::move(options)), id(fmt::format("{}", ::getpid())),
        compdbParser() {
    auto &compdbPath = this->options.compdbPath;
    if (!compdbPath.is_absolute()) {
      auto absPath = std::filesystem::current_path();
      absPath /= compdbPath;
      compdbPath = absPath;
    }

    MessageQueues::deleteIfPresent(this->id, this->numWorkers());
    this->queues = MessageQueues(this->id, this->numWorkers(),
                                 {IPC_BUFFER_MAX_SIZE, IPC_BUFFER_MAX_SIZE});
  }

  size_t numWorkers() const {
    return this->options.numWorkers;
  }
  const std::filesystem::path &compdbPath() const {
    return this->options.compdbPath;
  }
  std::chrono::seconds receiveTimeout() const {
    return this->options.receiveTimeout;
  }

  // NOTE: openCompilationDatabase should be called before this method.
  void spawnWorkers() {
    this->workers.resize(this->numWorkers());
    for (unsigned i = 0; i < this->numWorkers(); i++) {
      this->spawnWorker(i);
    }
  }

  size_t refillCount() const {
    return 2 * this->numWorkers();
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
    this->allJobList.insert({jobId, std::move(j)});
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
    std::error_code error;
    auto compdbFile =
        compdb::CompilationDatabaseFile::open(this->compdbPath(), error);
    if (!compdbFile.file) {
      spdlog::error("failed to open {}: {}", this->compdbPath().string(),
                    std::strerror(errno));
      std::exit(EXIT_FAILURE);
    }
    if (error) {
      spdlog::error("failed to read file size for compile_commands.json: {}",
                    error.message());
      std::exit(EXIT_FAILURE);
    }
    if (compdbFile.numJobs == 0) {
      spdlog::error("compile_commands.json has 0 objects in outermost array; "
                    "nothing to index");
      std::exit(EXIT_FAILURE);
    }
    this->totalJobCount = compdbFile.numJobs;
    this->options.numWorkers =
        std::min(this->totalJobCount, this->numWorkers());
    spdlog::debug("total {} compilation jobs", this->totalJobCount);

    this->compdbParser.initialize(compdbFile, this->refillCount());
    return FileGuard(compdbFile.file);
  }

private:
  void spawnWorker(WorkerId workerId) {
    std::vector<std::string> args;
    args.push_back(this->options.workerExecutablePath);
    args.push_back("--worker");
    args.push_back(fmt::format("--driver-id={}", this->id));
    args.push_back(fmt::format("--worker-id={}", workerId));
    this->options.addWorkerOptions(args);

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
    auto workerTimeout = this->receiveTimeout();

    IndexJobResponse response;
    auto recvError =
        this->queues.workerToDriver.timedReceive(response, workerTimeout);
    if (recvError.isA<TimeoutError>()) {
      spdlog::error("timeout from driver");
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

      // FIXME(def: add-driver-logic): We need to check the kind of the result
      // and potentially reassign a job to the worker.

      this->markWorkerFree(response.workerId);
      bool erased = wipJobs.erase(response.jobId);
      ENFORCE(erased, "received response for job not marked WIP");

      auto now = std::chrono::steady_clock::now();
      this->killLongRunningWorkersAndRespawn(now - workerTimeout);
    }
  }

  void assignJobToWorker(WorkerId workerId, JobId jobId) {
    // TODO(ref: add-job-debug-helper) Print abbreviated job data here.
    spdlog::debug("assigning jobId {} to worker {}", jobId.id(), workerId);
    this->wipJobs.insert(jobId);
    this->markWorkerBusy(workerId, jobId);
    auto it = this->allJobList.find(jobId);
    ENFORCE(it != this->allJobList.end(), "trying to assign unknown job");
    this->queues.driverToWorker[workerId].send(
        IndexJobRequest{it->first, IndexJob::clone(it->second)});
  }

  void assignJobsToAvailableWorkers() {
    auto numJobsToAssign =
        std::min(this->availableWorkers.size(), this->pendingJobs.size());
    ENFORCE(numJobsToAssign >= 1, "no workers or pending jobs");
    for (unsigned i = 0; i < numJobsToAssign; ++i) {
      JobId nextJob = this->pendingJobs.front();
      this->pendingJobs.pop_front();
      unsigned nextWorkerId = this->availableWorkers.front();
      this->availableWorkers.pop_front();
      this->assignJobToWorker(nextWorkerId, nextJob);
    }
  }

  void shutdownAllWorkers() {
    ENFORCE(this->availableWorkers.size() == this->numWorkers(),
            "shutdown should only happen after all workers finish processing");
    for (unsigned i = 0; i < this->numWorkers(); ++i) {
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

    Driver driver((DriverOptions(std::move(cliOptions))));
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