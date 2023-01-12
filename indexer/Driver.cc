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

using Instant = std::chrono::time_point<std::chrono::steady_clock>;

struct WorkerInfo {
  enum class Status {
    Busy,
    Idle,
  } status;

  boost::process::child processHandle;

  // Used when status == Busy
  Instant startTime;
  // Non-null when status == Busy
  std::optional<JobId> currentlyProcessing;

  WorkerInfo() = delete;
  WorkerInfo(WorkerInfo &&) = default;
  WorkerInfo &operator=(WorkerInfo &&) = default;
  WorkerInfo(const WorkerInfo &) = delete;
  WorkerInfo &operator=(const WorkerInfo &) = delete;

  WorkerInfo(boost::process::child &&newWorker)
      : status(Status::Idle), processHandle(std::move(newWorker)), startTime(),
        currentlyProcessing() {}
};

struct DriverOptions {
  std::string workerExecutablePath;
  StdPath compdbPath;
  size_t numWorkers;
  std::chrono::seconds receiveTimeout;
  bool deterministic;
  std::string preprocessorRecordHistoryFilterRegex;
  StdPath supplementaryOutputDir;
  std::string workerFault;

  explicit DriverOptions(const CliOptions &cliOpts)
      : workerExecutablePath(cliOpts.scipClangExecutablePath),
        compdbPath(cliOpts.compdbPath), numWorkers(cliOpts.numWorkers),
        receiveTimeout(cliOpts.receiveTimeout),
        deterministic(cliOpts.deterministic),
        preprocessorRecordHistoryFilterRegex(
            cliOpts.preprocessorRecordHistoryFilterRegex),
        supplementaryOutputDir(cliOpts.supplementaryOutputDir),
        workerFault(cliOpts.workerFault) {
    // NOTE: Constructor eagerly checks that the regex is well-formed
    HeaderFilter filter(
        (std::string(this->preprocessorRecordHistoryFilterRegex)));
    bool hasSupplementaryOutputs = !filter.isIdentity();
    if (!hasSupplementaryOutputs) {
      return;
    }
    std::error_code error;
    std::filesystem::create_directories(this->supplementaryOutputDir, error);
    if (!error) {
      return;
    }
    spdlog::error("failed to create supplementary output directory at '{}'",
                  this->supplementaryOutputDir.c_str());
    spdlog::error("I/O error: {}", error.message());
    std::exit(EXIT_FAILURE);
  }

  void addWorkerOptions(std::vector<std::string> &args,
                        WorkerId workerId) const {
    args.push_back(fmt::format(
        "--log-level={}", spdlog::level::to_string_view(spdlog::get_level())));
    static_assert(std::is_same<decltype(this->receiveTimeout),
                               std::chrono::seconds>::value);
    args.push_back(fmt::format("--receive-timeout-seconds={}",
                               this->receiveTimeout.count()));
    if (this->deterministic) {
      args.push_back("--deterministic");
    }
    if (!this->preprocessorRecordHistoryFilterRegex.empty()) {
      args.push_back(fmt::format("--preprocessor-record-history-filter={}",
                                 this->preprocessorRecordHistoryFilterRegex));
      auto logPath = this->supplementaryOutputDir;
      logPath.append(
          fmt::format("preprocessor-history-worker-{}.yaml", workerId));
      args.push_back(
          fmt::format("--preprocessor-history-log-path={}", logPath.c_str()));
    }
    if (!this->workerFault.empty()) {
      args.push_back("--force-worker-fault=" + this->workerFault);
    }
  }
};

/// Wrapper type over a WorkerId meant to be consumed exactly once.
template <typename T> class ConsumeOnce {
  T value;
  bool consumed;
  bool movedOut;

public:
  ConsumeOnce() = delete;
  ConsumeOnce(ConsumeOnce<T> &&old)
      : value(std::move(old.value)), consumed(old.consumed),
        movedOut(old.movedOut) {
    old.movedOut = true;
  }
  ConsumeOnce &operator=(ConsumeOnce &&old) {
    this->_id = std::move(old.value);
    this->consumed = old.consumed;
    this->movedOut = old.movedOut;
    old.movedOut = true;
    return *this;
  }
  ConsumeOnce(const ConsumeOnce &) = delete;
  ConsumeOnce &operator=(const ConsumeOnce &) = delete;

  ConsumeOnce(T &&value)
      : value(std::move(value)), consumed(false), movedOut(false) {}

  ~ConsumeOnce() {
    if (!this->movedOut) {
      ENFORCE(this->consumed, "forgot to call getValueAndConsume");
    }
  }
  T getValueAndConsume() {
    ENFORCE(!this->movedOut, "use after move");
    ENFORCE(!this->consumed, "trying to consume worker guard twice");
    this->consumed = true;
    return std::move(this->value);
  }
  const T &getValueNonConsuming() {
    ENFORCE(!this->movedOut, "use after move");
    ENFORCE(!this->consumed, "trying to access id for consumed guard");
    return this->value;
  }
};

/// Type for indicating that a worker is not longer idle
/// and will imminently be scheduled.
using ToBeScheduledWorkerId = ConsumeOnce<WorkerId>;

class Scheduler final {
  std::vector<WorkerInfo> workers;
  /// Keep track of which workers are available in FIFO order.
  /// Values are indexes into \c workers.
  std::deque<unsigned> idleWorkers;

  /// Monotonically growing counter.
  uint64_t nextJobId = 0;
  /// Monotonically growing map of all jobs that have been created so far.
  /// This number will generally be unrelated to \c compdbCommandCount
  /// because a single compilation database entry will typically lead
  /// to creation of multiple jobs.
  ///
  /// In principle, after a job is completed, we could start removing
  /// entries from this map, but leaving them around for debugging.
  absl::flat_hash_map<JobId, IndexJob> allJobList;

  /// FIFO queue holding jobs which haven't been scheduled yet.
  /// Elements must be valid keys in allJobList.
  ///
  /// ∀ j ∈ pendingJobs, ∄ w ∈ workers. w.currentlyProcessing == p
  std::deque<JobId> pendingJobs;

  /// Jobs that have been scheduled but not known to be completed.
  /// Elements must be valid keys in allJobList.
  ///
  /// ∀ j ∈ pendingJobs, |{w ∈ workers | w.currentlyProcessing == p}| == 1
  absl::flat_hash_set<JobId> wipJobs;

public:
  using Process = boost::process::child;

  void checkInvariants() const {
    ENFORCE(
        this->wipJobs.size() + this->idleWorkers.size() == this->workers.size(),
        "wipJobs.size() ({}) + idleWorkers.size() ({}) != "
        "workers.size() ({})",
        this->wipJobs.size(), this->idleWorkers.size(), this->workers.size());
  }

  /// \p spawn should only create the process; it should not call back
  /// into the Scheduler (to make reasoning about Scheduler state changes
  /// easier).
  void initializeWorkers(size_t numWorkers,
                         absl::FunctionRef<Process(WorkerId workerId)> spawn) {
    this->workers.clear();
    this->workers.reserve(numWorkers);
    for (size_t workerId = 0; workerId < numWorkers; ++workerId) {
      boost::process::child worker = spawn(workerId);
      this->workers.emplace_back(WorkerInfo(std::move(worker)));
      this->idleWorkers.push_back(workerId);
    }
    this->checkInvariants();
  }

  /// Kills all workers which started before \p startedBefore and respawns them.
  ///
  /// \p killAndRespawn should not call back into the Scheduler (to make
  /// reasoning about Scheduler state changes easier).
  void killLongRunningWorkersAndRespawn(
      Instant startedBefore,
      absl::FunctionRef<Process(Process &&, WorkerId)> killAndRespawn) {
    this->checkInvariants();
    // NOTE: N_workers <= 500. On the fast path, this boils down to
    // N_workers indexing ops + integer comparisons, so it should be cheap.
    for (unsigned workerId = 0; workerId < this->workers.size(); ++workerId) {
      auto &workerInfo = this->workers[workerId];
      switch (workerInfo.status) {
      case WorkerInfo::Status::Idle:
        continue;
      case WorkerInfo::Status::Busy:
        if (workerInfo.startTime < startedBefore) {
          spdlog::info("killing worker {}, pid {}", workerId,
                       workerInfo.processHandle.id());
          auto oldJobId = workerInfo.currentlyProcessing.value();
          bool erased = this->wipJobs.erase(oldJobId);
          ENFORCE(erased, "*worker.currentlyProcessing was not marked WIP");
          spdlog::warn("skipping job {} due to worker timeout", oldJobId.id());
          auto newHandle =
              killAndRespawn(std::move(workerInfo.processHandle), workerId);
          workerInfo = WorkerInfo(std::move(newHandle));
          this->idleWorkers.push_back(workerId);
          this->checkInvariants();
        }
      }
    }
  }

  void waitForAllWorkers() {
    for (auto &worker : this->workers) {
      worker.processHandle.wait();
    }
  }

  void queueJob(IndexJob &&j) {
    auto jobId = JobId(this->nextJobId);
    this->nextJobId++;
    this->allJobList.insert({jobId, std::move(j)});
    this->pendingJobs.push_back(jobId);
  }

  [[nodiscard]] IndexJobRequest
  scheduleJobOnWorker(ToBeScheduledWorkerId &&workerId, JobId jobId) {
    ENFORCE(absl::c_find(this->idleWorkers, workerId.getValueNonConsuming())
            == this->idleWorkers.end());
    // TODO(ref: add-job-debug-helper) Print abbreviated job data here.
    spdlog::debug("assigning jobId {} to worker {}", jobId.id(),
                  workerId.getValueNonConsuming());
    ENFORCE(this->wipJobs.contains(jobId),
            "should've marked job WIP before scheduling");
    this->markWorkerBusy(std::move(workerId), jobId);
    auto it = this->allJobList.find(jobId);
    ENFORCE(it != this->allJobList.end(), "trying to assign unknown job");
    return IndexJobRequest{it->first, it->second};
  }

  void markCompleted(WorkerId workerId, JobId jobId, IndexJob::Kind kind) {
    ENFORCE(this->workers[workerId].currentlyProcessing == jobId);
    this->markWorkerIdle(workerId);
    bool erased = wipJobs.erase(jobId);
    ENFORCE(erased, "received response for job not marked WIP");
    ENFORCE(this->allJobList[jobId].kind == kind);
  }

  /// Pre-condition: \p refillJobs should stay fixed at 0 once it reaches 0.
  void
  runJobsTillCompletion(absl::FunctionRef<void()> processOneJobResult,
                        absl::FunctionRef<size_t()> refillJobs,
                        absl::FunctionRef<void(ToBeScheduledWorkerId &&, JobId)>
                            assignJobToWorker) {
    this->checkInvariants();
    size_t refillCount = refillJobs();
    ENFORCE(refillCount > 0);
    ENFORCE(this->pendingJobs.size() == refillCount);
    // NOTE(def: scheduling-invariant):
    // Jobs are refilled into the pending jobs list before WIP jobs are
    // marked as completed. This means that if there is at least one TU
    // which hasn't been indexed yet, then
    //   pendingJobs.size() != 0 && wipJobs.size() != 0
    while (true) {
      this->checkInvariants();
      if (this->pendingJobs.empty()) {
        if (this->wipJobs.empty()) {
          break;
        } else if (refillCount != 0) {
          refillCount = refillJobs();
        }
      } else if (!this->idleWorkers.empty()) {
        this->assignJobsToIdleWorkers(assignJobToWorker);
      }
      ENFORCE(!this->wipJobs.empty());
      processOneJobResult();
    }
    this->checkInvariants();
    ENFORCE(this->idleWorkers.size() == this->workers.size(),
            "all workers should be idle after jobs have been completed");
  }

private:
  ToBeScheduledWorkerId claimIdleWorker() {
    ENFORCE(!this->idleWorkers.empty());
    WorkerId workerId = this->idleWorkers.front();
    this->idleWorkers.pop_front();
    return ToBeScheduledWorkerId(std::move(workerId));
  }

  /// Dual to \c claimAvailableWorker.
  void markWorkerIdle(WorkerId workerId) {
    auto &workerInfo = this->workers[workerId];
    ENFORCE(workerInfo.currentlyProcessing.has_value());
    workerInfo.currentlyProcessing = {};
    ENFORCE(workerInfo.status == WorkerInfo::Status::Busy);
    workerInfo.status = WorkerInfo::Status::Idle;
    this->idleWorkers.push_front(workerId);
  }

  void markWorkerBusy(ToBeScheduledWorkerId &&workerId, JobId newJobId) {
    auto &nextWorkerInfo = this->workers[workerId.getValueAndConsume()];
    ENFORCE(nextWorkerInfo.status == WorkerInfo::Status::Idle);
    nextWorkerInfo.status = WorkerInfo::Status::Busy;
    ENFORCE(!nextWorkerInfo.currentlyProcessing.has_value());
    nextWorkerInfo.currentlyProcessing = {newJobId};
    nextWorkerInfo.startTime = std::chrono::steady_clock::now();
  }

  void assignJobsToIdleWorkers(
      absl::FunctionRef<void(ToBeScheduledWorkerId &&, JobId)> assignJob) {
    auto numJobsToAssign =
        std::min(this->idleWorkers.size(), this->pendingJobs.size());
    ENFORCE(numJobsToAssign >= 1, "no workers or pending jobs");
    for (unsigned i = 0; i < numJobsToAssign; ++i) {
      JobId nextJob = this->pendingJobs.front();
      this->pendingJobs.pop_front();
      auto [_, inserted] = this->wipJobs.insert(nextJob);
      ENFORCE(inserted, "job from pendingJobs was not already WIP");
      auto nextWorkerId = this->claimIdleWorker();
      assignJob(std::move(nextWorkerId), nextJob);
      this->checkInvariants();
    }
  }
};

/// Type responsible for administrative tasks like timeouts, progressively
/// queueing jobs and killing misbehaving workers.
class Driver {
  DriverOptions options;
  std::string id;
  MessageQueues queues;
  Scheduler scheduler;

  /// Total number of commands in the compilation database.
  size_t compdbCommandCount = 0;
  compdb::ResumableParser compdbParser;

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
  const StdPath &compdbPath() const {
    return this->options.compdbPath;
  }
  std::chrono::seconds receiveTimeout() const {
    return this->options.receiveTimeout;
  }

  /// Call \c openCompilationDatabase before this method. The \p _compdbToken
  /// parameter is present to accidentally avoid flipping call order.
  void spawnWorkers(const FileGuard &_compdbToken) {
    this->scheduler.initializeWorkers(
        this->numWorkers(), [&](WorkerId workerId) -> Scheduler::Process {
          return this->spawnWorker(workerId);
        });
  }

  size_t refillCount() const {
    return 2 * this->numWorkers();
  }

  size_t refillJobs() {
    std::vector<clang::tooling::CompileCommand> commands{};
    this->compdbParser.parseMore(commands);
    for (auto &command : commands) {
      this->scheduler.queueJob(
          IndexJob{IndexJob::Kind::SemanticAnalysis,
                   SemanticAnalysisJobDetails{std::move(command)},
                   EmitIndexJobDetails{}});
    }
    return commands.size();
  }

  void runJobsTillCompletionAndShutdownWorkers() {
    this->scheduler.runJobsTillCompletion(
        [this]() -> void { this->processOneJobResult(); },
        [this]() -> size_t { return this->refillJobs(); },
        [this](ToBeScheduledWorkerId &&workerId, JobId jobId) -> void {
          this->assignJobToWorker(std::move(workerId), jobId);
        });
    this->shutdownAllWorkers();
    this->scheduler.waitForAllWorkers();
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
    if (compdbFile.commandCount == 0) {
      spdlog::error("compile_commands.json has 0 objects in outermost array; "
                    "nothing to index");
      std::exit(EXIT_FAILURE);
    }
    this->compdbCommandCount = compdbFile.commandCount;
    this->options.numWorkers =
        std::min(this->compdbCommandCount, this->numWorkers());
    spdlog::debug("total {} compilation jobs", this->compdbCommandCount);

    this->compdbParser.initialize(compdbFile, this->refillCount());
    return FileGuard(compdbFile.file);
  }

private:
  boost::process::child spawnWorker(WorkerId workerId) {
    std::vector<std::string> args;
    args.push_back(this->options.workerExecutablePath);
    args.push_back("--worker");
    args.push_back(fmt::format("--driver-id={}", this->id));
    args.push_back(fmt::format("--worker-id={}", workerId));
    this->options.addWorkerOptions(args, workerId);

    boost::process::child worker(args, boost::process::std_out > stdout);
    spdlog::debug("worker info running {}, pid = {}", worker.running(),
                  worker.id());
    return worker;
  }

  /// Kills all workers which started before \p startedBefore and respawns them.
  void killLongRunningWorkersAndRespawn(Instant startedBefore) {
    this->scheduler.killLongRunningWorkersAndRespawn(
        startedBefore,
        [&](Scheduler::Process &&oldHandle,
            WorkerId workerId) -> Scheduler::Process {
          oldHandle.terminate();
          return this->spawnWorker(workerId);
        });
  }

  void processSemanticAnalysisResult(SemanticAnalysisJobResult &&result) {}

  void processWorkerResponse(IndexJobResponse &&response) {
    this->scheduler.markCompleted(response.workerId, response.jobId,
                                  response.result.kind);
    // TODO: Implement logic to process the result
    switch (response.result.kind) {
    case IndexJob::Kind::SemanticAnalysis:
      break;
    case IndexJob::Kind::EmitIndex:
      break;
    }
  }

  void processOneJobResult() {
    using namespace std::chrono_literals;
    auto workerTimeout = this->receiveTimeout();

    IndexJobResponse response;
    auto recvError =
        this->queues.workerToDriver.timedReceive(response, workerTimeout);
    if (recvError.isA<TimeoutError>()) {
      spdlog::warn("timeout: no workers have responded yet");
      // All workers which are working have been doing so for too long,
      // because TimeoutError means we already exceeded the timeout limit.
    } else if (recvError) {
      spdlog::error("received malformed message: {}",
                    scip_clang::formatLlvm(recvError));
      // Keep going instead of exiting early for robustness.
    } else {
      // TODO(def: add-job-debug-helper): Add a simplified debug representation
      // for printing jobs for debugging.
      spdlog::debug("received job from worker {}", response.workerId);

      this->processWorkerResponse(std::move(response));
    }
    auto now = std::chrono::steady_clock::now();
    this->killLongRunningWorkersAndRespawn(now - workerTimeout);
  }

  // Assign a job to a specific worker. When this method is called,
  // the worker has already been "claimed", so it should not be in the
  // availableWorkers list.
  void assignJobToWorker(ToBeScheduledWorkerId &&workerId, JobId jobId) {
    auto &queue = this->queues.driverToWorker[workerId.getValueNonConsuming()];
    queue.send(this->scheduler.scheduleJobOnWorker(std::move(workerId), jobId));
  }

  void shutdownAllWorkers() {
    for (unsigned i = 0; i < this->numWorkers(); ++i) {
      this->queues.driverToWorker[i].send(
          IndexJobRequest{JobId::Shutdown(), {}});
    }
  }
};

} // namespace

int driverMain(CliOptions &&cliOptions) {
  pid_t driverPid = ::getpid();
  auto driverId = fmt::format("{}", driverPid);
  size_t numWorkers = cliOptions.numWorkers;
  BOOST_TRY {
    Driver driver((DriverOptions(std::move(cliOptions))));
    auto compdbGuard = driver.openCompilationDatabase();
    driver.spawnWorkers(compdbGuard);
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
