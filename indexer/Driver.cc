#include <algorithm>
#include <chrono>
#include <compare>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "indexer/Enforce.h" // Defines ENFORCE required by rapidjson headers

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_join.h"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/search_path.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

#include "llvm/ADT/StringMap.h"

#include "scip/scip.pb.h"

#include "indexer/CliOptions.h"
#include "indexer/Comparison.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/Driver.h"
#include "indexer/FileSystem.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Logging.h"
#include "indexer/Path.h"
#include "indexer/RAII.h"
#include "indexer/ScipExtras.h"
#include "indexer/Statistics.h"
#include "indexer/Timer.h"

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
  AbsolutePath workerExecutablePath;
  RootPath projectRootPath;
  AbsolutePath compdbPath;
  AbsolutePath indexOutputPath;
  AbsolutePath statsFilePath;
  size_t numWorkers;
  std::chrono::seconds receiveTimeout;
  bool deterministic;
  std::string preprocessorRecordHistoryFilterRegex;
  StdPath supplementaryOutputDir;
  std::string workerFault;
  bool isTesting;

  StdPath temporaryOutputDir;
  bool deleteTemporaryOutputDir;

  std::vector<std::string> originalArgv;

  explicit DriverOptions(std::string driverId, const CliOptions &cliOpts)
      : workerExecutablePath(),
        projectRootPath(AbsolutePath("/"), RootKind::Project), compdbPath(),
        indexOutputPath(), statsFilePath(), numWorkers(cliOpts.numWorkers),
        receiveTimeout(cliOpts.receiveTimeout),
        deterministic(cliOpts.deterministic),
        preprocessorRecordHistoryFilterRegex(
            cliOpts.preprocessorRecordHistoryFilterRegex),
        supplementaryOutputDir(cliOpts.supplementaryOutputDir),
        workerFault(cliOpts.workerFault), isTesting(cliOpts.isTesting),
        temporaryOutputDir(cliOpts.temporaryOutputDir),
        deleteTemporaryOutputDir(cliOpts.temporaryOutputDir.empty()),
        originalArgv(cliOpts.originalArgv) {

    auto cwd = std::filesystem::current_path().string();
    ENFORCE(llvm::sys::path::is_absolute(cwd),
            "std::filesystem::current_path() returned non-absolute path '{}'",
            cwd);
    this->projectRootPath =
        RootPath{AbsolutePath{std::move(cwd)}, RootKind::Project};

    auto setAbsolutePath = [this](const std::string &path, AbsolutePath &out) {
      out = path.empty()
                ? AbsolutePath()
                : (llvm::sys::path::is_absolute(path)
                       ? AbsolutePath(std::string(path))
                       : this->projectRootPath.makeAbsolute(
                           RootRelativePathRef(path, RootKind::Project)));
    };

    // Strictly speaking, there is a TOCTOU problem here, as scip-clang
    // can go missing between this check and the actual execve invocation
    // when spawning a worker, but it's simpler to check this here and
    // provide a nicer error message.
    if (cliOpts.scipClangExecutablePath.find(
            std::filesystem::path::preferred_separator)
        == std::string::npos) {
      auto newPath =
          boost::process::search_path(cliOpts.scipClangExecutablePath);
      if (newPath.empty()) {
        spdlog::error("scip-clang needs to be able to re-invoke itself,"
                      " but couldn't find scip-clang on PATH."
                      " Hint: Use a modified PATH to invoke scip-clang,"
                      " or invoke scip-clang using an absolute path");
        std::exit(1);
      }
      this->workerExecutablePath = AbsolutePath(std::string(newPath.native()));
    } else {
      setAbsolutePath(cliOpts.scipClangExecutablePath,
                      this->workerExecutablePath);
    }

    setAbsolutePath(cliOpts.indexOutputPath, this->indexOutputPath);
    setAbsolutePath(cliOpts.compdbPath, this->compdbPath);
    setAbsolutePath(cliOpts.statsFilePath, this->statsFilePath);

    auto makeDirs = [](const StdPath &path, const char *name) {
      std::error_code error;
      std::filesystem::create_directories(path, error);
      if (error) {
        spdlog::error("failed to create {} at '{}' ({})", name, path.c_str(),
                      error.message());
        std::exit(EXIT_FAILURE);
      }
    };

    // NOTE: Constructor eagerly checks that the regex is well-formed
    HeaderFilter filter(
        (std::string(this->preprocessorRecordHistoryFilterRegex)));
    bool hasSupplementaryOutputs = !filter.isIdentity();
    if (hasSupplementaryOutputs) {
      makeDirs(this->supplementaryOutputDir, "supplementary output directory");
    }

    if (this->temporaryOutputDir.empty()) {
      this->temporaryOutputDir = std::filesystem::temp_directory_path();
      if (this->temporaryOutputDir.empty()) {
        this->temporaryOutputDir = "scip-clang-temporary-output";
      }
      this->temporaryOutputDir.append("scip-clang-" + driverId);
    }
    makeDirs(this->temporaryOutputDir, "temporary output directory");
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
    if (!this->statsFilePath.asStringRef().empty()) {
      args.push_back("--measure-statistics");
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
    ENFORCE(!this->temporaryOutputDir.empty());
    args.push_back(fmt::format("--temporary-output-dir={}",
                               this->temporaryOutputDir.c_str()));
  }
};

/// Type for indicating that a worker is not longer idle
/// and will imminently be scheduled.
using ToBeScheduledWorkerId = ConsumeOnce<WorkerId>;

/// Wrapper type that indicates a WorkerId was just marked idle,
/// and hence can be directly be assigned a job using
/// \c Scheduler::createSubtaskAndScheduleOnWorker
struct LatestIdleWorkerId {
  WorkerId id;
};

/// Type that decides which files to emit symbols and occurrences for
/// given a set of paths+hashes emitted by a worker.
///
/// NOTE(def: header-recovery) We are assuming here that we will be
/// normally be able to successfully index a large fraction of code,
/// so don't complicate the code for fault tolerance. For example,
/// right now, if we assigned a header to a specific worker, and
/// that worker crashed, then the header would never be considered
/// for indexing in the future.
///
/// In principle, we could maintain a list of jobs which involved
/// that header, and later spin up new indexing jobs which forced
/// indexing of that header. However, that would make the code more
/// complex, so let's skip that for now.
class FileIndexingPlanner {
  absl::flat_hash_map<AbsolutePath, absl::flat_hash_set<HashValue>> hashesSoFar;
  const RootPath &projectRootPath;

public:
  FileIndexingPlanner(const RootPath &projectRootPath)
      : hashesSoFar(), projectRootPath(projectRootPath) {}
  FileIndexingPlanner(FileIndexingPlanner &&) = default;
  FileIndexingPlanner(const FileIndexingPlanner &) = delete;

  void saveSemaResult(SemanticAnalysisJobResult &&semaResult,
                      std::vector<PreprocessedFileInfo> &filesToBeIndexed) {
    absl::flat_hash_set<HashValue> emptyHashSet{};
    for (auto &fileInfoMulti : semaResult.illBehavedFiles) {
      auto [it, _] = hashesSoFar.insert(
          {AbsolutePath(std::move(fileInfoMulti.path)), emptyHashSet});
      auto &[path, hashes] = *it;
      for (auto hashValue : fileInfoMulti.hashValues) {
        auto [_, inserted] = hashes.insert(hashValue);
        if (inserted) {
          filesToBeIndexed.push_back({path, hashValue}); // deliberate copy
        }
      }
    }
    for (auto &fileInfo : semaResult.wellBehavedFiles) {
      auto [it, _] = hashesSoFar.insert(
          {AbsolutePath(std::move(fileInfo.path)), emptyHashSet});
      auto &[path, hashes] = *it;
      auto [__, inserted] = hashes.insert(fileInfo.hashValue);
      if (inserted) {
        filesToBeIndexed.push_back(
            {path, fileInfo.hashValue}); // deliberate copy
      }
    }
  }

  bool isMultiplyIndexed(RootRelativePathRef relativePath) const {
    auto absPath = this->projectRootPath.makeAbsolute(relativePath);
    auto it = this->hashesSoFar.find(absPath);
    if (it == this->hashesSoFar.end()) {
      ENFORCE(false, "found path '{}' with no recorded hashes",
              relativePath.asStringView());
      return false;
    }
    return it->second.size() > 1;
  }
};

class Scheduler final {
  std::vector<WorkerInfo> workers;
  /// Keep track of which workers are available in FIFO order.
  /// Values are indexes into \c workers.
  std::deque<unsigned> idleWorkers;

  /// Monotonically growing counter.
  uint32_t nextTaskId = 0;
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

  const absl::flat_hash_map<JobId, IndexJob> &getJobMap() const {
    return this->allJobList;
  }

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

  void logJobSkip(JobId jobId) const {
    spdlog::info("the worker was {}", [&]() -> std::string {
      auto it = this->allJobList.find(jobId);
      ENFORCE(it != this->allJobList.end());
      switch (it->second.kind) {
      case IndexJob::Kind::SemanticAnalysis:
        return fmt::format("running semantic analysis for '{}'",
                           it->second.semanticAnalysis.command.Filename);
      case IndexJob::Kind::EmitIndex:
        auto &fileInfos = it->second.emitIndex.filesToBeIndexed;
        auto fileInfoIt = absl::c_find_if(
            fileInfos, [](const PreprocessedFileInfo &fileInfo) -> bool {
              auto &sv = fileInfo.path.asStringRef();
              return sv.ends_with(".c") || sv.ends_with(".cc")
                     || sv.ends_with(".cxx") || sv.ends_with(".cpp");
            });
        if (fileInfoIt != fileInfos.end()) {
          return fmt::format("emitting an index for '{}'",
                             fileInfoIt->path.asStringRef());
        }
        return "emitting a partial index";
      }
    }());
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
          spdlog::warn("skipping job {} due to worker timeout",
                       oldJobId.debugString());
          this->logJobSkip(oldJobId);
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

  void queueNewTask(IndexJob &&j) {
    auto jobId = JobId::newTask(this->nextTaskId);
    this->nextTaskId++;
    this->allJobList.insert({jobId, std::move(j)});
    this->pendingJobs.push_back(jobId);
  }

  [[nodiscard]] IndexJobRequest
  createSubtaskAndScheduleOnWorker(LatestIdleWorkerId workerId,
                                   JobId previousId, IndexJob &&job) {
    auto jobId = previousId.nextSubtask();
    this->allJobList.insert({jobId, std::move(job)});
    this->wipJobs.insert(jobId);
    ENFORCE(!this->idleWorkers.empty());
    ENFORCE(this->idleWorkers.front() == workerId.id);
    this->idleWorkers.pop_front();
    return this->scheduleJobOnWorker(
        ToBeScheduledWorkerId(std::move(workerId.id)), jobId);
  }

  [[nodiscard]] IndexJobRequest
  scheduleJobOnWorker(ToBeScheduledWorkerId &&workerId, JobId jobId) {
    ENFORCE(absl::c_find(this->idleWorkers, workerId.getValueNonConsuming())
            == this->idleWorkers.end());
    // TODO(ref: add-job-debug-helper) Print abbreviated job data here.
    spdlog::debug("assigning jobId {} to worker {}", jobId.debugString(),
                  workerId.getValueNonConsuming());
    ENFORCE(this->wipJobs.contains(jobId),
            "should've marked job WIP before scheduling");
    this->markWorkerBusy(std::move(workerId), jobId);
    auto it = this->allJobList.find(jobId);
    ENFORCE(it != this->allJobList.end(), "trying to assign unknown job");
    return IndexJobRequest{it->first, it->second};
  }

  [[nodiscard]] LatestIdleWorkerId markCompleted(WorkerId workerId, JobId jobId,
                                                 IndexJob::Kind responseKind) {
    ENFORCE(this->workers[workerId].currentlyProcessing == jobId);
    this->markWorkerIdle(workerId);
    bool erased = wipJobs.erase(jobId);
    ENFORCE(erased, "received response for job not marked WIP");
    ENFORCE(this->allJobList[jobId].kind == responseKind);
    return LatestIdleWorkerId{workerId};
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
  FileIndexingPlanner planner;

  std::vector<std::pair<JobId, IndexingStatistics>> allStatistics;
  std::vector<AbsolutePath> indexPartPaths;

  /// Total number of commands in the compilation database.
  size_t compdbCommandCount = 0;
  compdb::ResumableParser compdbParser;

public:
  Driver(const Driver &) = delete;
  Driver &operator=(const Driver &) = delete;

  Driver(std::string driverId, DriverOptions &&options)
      : options(std::move(options)), id(driverId), scheduler(),
        planner(this->options.projectRootPath), indexPartPaths(),
        compdbParser() {
    MessageQueues::deleteIfPresent(this->id, this->numWorkers());
    this->queues = MessageQueues(this->id, this->numWorkers(),
                                 {IPC_BUFFER_MAX_SIZE, IPC_BUFFER_MAX_SIZE});
  }
  ~Driver() {
    if (this->options.deleteTemporaryOutputDir) {
      std::error_code error;
      std::filesystem::remove_all(this->options.temporaryOutputDir, error);
      if (error) {
        spdlog::warn("failed to remove temporary output directory at '{}' ({})",
                     this->options.temporaryOutputDir.c_str(), error.message());
      }
    }
  }

  void emitStatsFile() {
    if (this->options.statsFilePath.asStringRef().empty()) {
      return;
    }
    std::vector<std::pair<uint32_t, StatsEntry>> perJobStats{};
    auto &jobMap = this->scheduler.getJobMap();
    for (auto &pair : this->allStatistics) {
      auto &[jobId, stats] = pair;
      auto semaJobId = JobId::newTask(jobId.taskId());
      auto it = jobMap.find(semaJobId);
      ENFORCE(it != jobMap.end());
      ENFORCE(it->second.kind == IndexJob::Kind::SemanticAnalysis);
      perJobStats.emplace_back(
          jobId.taskId(),
          StatsEntry{it->second.semanticAnalysis.command.Filename,
                     std::move(stats)});
    }
    absl::c_sort(perJobStats, [](const auto &p1, const auto &p2) -> bool {
      ENFORCE(p1.first != p2.first,
              "got multiple StatEntry values for the same TU");
      return p1.first < p2.first;
    });
    std::vector<StatsEntry> stats{};
    for (auto &pair : perJobStats) {
      stats.emplace_back(std::move(pair.second));
    }
    StatsEntry::emitAll(std::move(stats),
                        this->options.statsFilePath.asStringRef());
  }

  void run() {
    ManualTimer total, indexing, merging;
    unsigned numTus;

    TIME_IT(total, {
      auto compdbGuard = this->openCompilationDatabase();
      this->spawnWorkers(compdbGuard);
      TIME_IT(indexing,
              numTus = this->runJobsTillCompletionAndShutdownWorkers());
      TIME_IT(merging, this->emitScipIndex());
      spdlog::debug("indexing complete; driver shutting down now, kthxbai");
    });
    this->emitStatsFile();

    using secs = std::chrono::seconds;
    fmt::print("Finished indexing {} translation units in {:.1f}s (indexing: "
               "{:.1f}s, merging: {:.1f}s).\n",
               numTus, total.value<secs>(), indexing.value<secs>(),
               merging.value<secs>());
  }

private:
  void emitScipIndex() {
    auto &indexScipPath = this->options.indexOutputPath;
    std::ofstream outputStream(indexScipPath.asStringRef(),
                               std::ios_base::out | std::ios_base::binary
                                   | std::ios_base::trunc);
    if (outputStream.fail()) {
      spdlog::error("failed to open '{}' for writing index ({})",
                    indexScipPath.asStringRef(), std::strerror(errno));
      std::exit(EXIT_FAILURE);
    }

    scip::Index fullIndex{};
    if (this->options.deterministic) {
      // Sorting before merging so that mergeIndexParts can be const
      absl::c_sort(this->indexPartPaths,
                   [](const AbsolutePath &p1, const AbsolutePath &p2) -> bool {
                     auto cmp = p1 <=> p2;
                     ENFORCE(cmp != 0, "2+ index parts have same path '{}'",
                             p1.asStringRef());
                     return cmp == std::strong_ordering::less;
                   });
    }
    this->mergeIndexParts(fullIndex);
    fullIndex.SerializeToOstream(&outputStream);
  }

  void mergeIndexParts(scip::Index &fullIndex) const {
    LogTimerRAII timer("index merging");

    scip::ToolInfo toolInfo;
    toolInfo.set_name("scip-clang");
    toolInfo.set_version("0.0.0"); // See TODO(ref: add-version)
    for (auto &arg : this->options.originalArgv) {
      toolInfo.add_arguments(arg);
    }

    scip::Metadata metadata;
    auto projectRootUnixStyle = llvm::sys::path::convert_to_slash(
        this->options.projectRootPath.asRef().asStringView());
    metadata.set_project_root("file:/" + projectRootUnixStyle);
    metadata.set_version(scip::UnspecifiedProtocolVersion);
    metadata.set_text_document_encoding(scip::TextEncoding::UTF8);
    *metadata.mutable_tool_info() = std::move(toolInfo);

    // TODO(def: faster-index-merging): Right now, the index merging
    // implementation has the overhead of serializing + deserializing all data
    // twice (once in the worker and once in the driver). In principle, we
    // could have a fast path for the common case (most documents always
    // have the same hash) by taking advantage of Protobuf's wire format.
    // Specifically, we can avoid deserializing well-behaved Documents,
    // and just concatenate them. (We still need to merge the external
    // symbols because different index parts may have different information
    // about external symbols). However, that is more finicky to do,
    // so we should measure the overhead before doing that.
    //
    // The implementation is also fully serial to avoid introducing
    // a dependency on a library with a concurrent hash table.
    *fullIndex.mutable_metadata() = std::move(metadata);

    scip::IndexBuilder builder{fullIndex};
    // TODO: Measure how much time this is taking and parallelize if too slow.
    for (auto &indexPartAbsPath : this->indexPartPaths) {
      auto &indexPartPath = indexPartAbsPath.asStringRef();
      std::ifstream inputStream(indexPartPath,
                                std::ios_base::in | std::ios_base::binary);
      if (inputStream.fail()) {
        spdlog::warn("failed to open partial index at '{}' ({})", indexPartPath,
                     std::strerror(errno));
        continue;
      }
      scip::Index partialIndex;
      if (!partialIndex.ParseFromIstream(&inputStream)) {
        spdlog::warn("failed to parse partial index at '{}'", indexPartPath);
        continue;
      }
      for (auto &doc : *partialIndex.mutable_documents()) {
        bool isMultiplyIndexed = this->planner.isMultiplyIndexed(
            RootRelativePathRef{doc.relative_path(), RootKind::Project});
        builder.addDocument(std::move(doc), isMultiplyIndexed);
      }
      // See NOTE(ref: precondition-deterministic-ext-symbol-docs); in
      // deterministic mode, indexes should be the same, and iterated over in
      // sorted order. So if external symbol emission in each part is
      // deterministic, addExternalSymbol will be called in deterministic order.
      for (auto &extSym : *partialIndex.mutable_external_symbols()) {
        builder.addExternalSymbol(std::move(extSym));
      }
    }
    builder.finish(this->options.deterministic);
  }

  size_t numWorkers() const {
    return this->options.numWorkers;
  }
  const AbsolutePath &compdbPath() const {
    return this->options.compdbPath;
  }
  std::chrono::seconds receiveTimeout() const {
    return this->options.receiveTimeout;
  }

  /// Call \c openCompilationDatabase before this method. The \p _compdbToken
  /// parameter is present to accidentally avoid flipping call order.
  void spawnWorkers(const FileGuard &_compdbToken) {
    (void)_compdbToken;
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
      this->scheduler.queueNewTask(
          IndexJob{IndexJob::Kind::SemanticAnalysis,
                   SemanticAnalysisJobDetails{std::move(command)},
                   EmitIndexJobDetails{}});
    }
    return commands.size();
  }

  /// Returns the number of TUs processed
  unsigned runJobsTillCompletionAndShutdownWorkers() {
    unsigned numJobs = 0;
    this->scheduler.runJobsTillCompletion(
        [this, &numJobs]() -> void {
          this->processOneJobResult();
          numJobs++;
        },
        [this]() -> size_t { return this->refillJobs(); },
        [this](ToBeScheduledWorkerId &&workerId, JobId jobId) -> void {
          this->assignJobToWorker(std::move(workerId), jobId);
        });
    this->shutdownAllWorkers();
    this->scheduler.waitForAllWorkers();
    return numJobs / 2; // Each TU has exactly 2 jobs.
  }

  FileGuard openCompilationDatabase() {
    std::error_code error;
    StdPath compdbStdPath{this->compdbPath().asStringRef()};
    auto compdbFile = compdb::CompilationDatabaseFile::openAndExitOnErrors(
        compdbStdPath,
        compdb::ValidationOptions{.checkDirectoryPathsAreAbsolute =
                                      !this->options.isTesting});
    this->compdbCommandCount = compdbFile.commandCount();
    this->options.numWorkers =
        std::min(this->compdbCommandCount, this->numWorkers());
    spdlog::debug("total {} compilation jobs", this->compdbCommandCount);

    this->compdbParser.initialize(compdbFile, this->refillCount());
    return FileGuard(compdbFile.file);
  }

  boost::process::child spawnWorker(WorkerId workerId) {
    std::vector<std::string> args;
    args.push_back(this->options.workerExecutablePath.asStringRef());
    args.push_back("--worker-mode=ipc");
    args.push_back(fmt::format("--driver-id={}", this->id));
    args.push_back(fmt::format("--worker-id={}", workerId));
    this->options.addWorkerOptions(args, workerId);

    spdlog::debug("spawning worker with arguments: '{}'",
                  absl::StrJoin(args, " "));

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

  void processSemanticAnalysisResult(SemanticAnalysisJobResult &&) {}

  void processWorkerResponse(IndexJobResponse &&response) {
    auto latestIdleWorkerId = this->scheduler.markCompleted(
        response.workerId, response.jobId, response.result.kind);
    switch (response.result.kind) {
    case IndexJob::Kind::SemanticAnalysis: {
      auto &semaResult = response.result.semanticAnalysis;
      std::vector<PreprocessedFileInfo> filesToBeIndexed{};
      this->planner.saveSemaResult(std::move(semaResult), filesToBeIndexed);
      auto &queue = this->queues.driverToWorker[latestIdleWorkerId.id];
      queue.send(this->scheduler.createSubtaskAndScheduleOnWorker(
          latestIdleWorkerId, response.jobId,
          IndexJob{
              .kind = IndexJob::Kind::EmitIndex,
              .emitIndex = EmitIndexJobDetails{std::move(filesToBeIndexed)},
          }));
      break;
    }
    case IndexJob::Kind::EmitIndex: {
      auto &result = response.result.emitIndex;
      if (!this->options.statsFilePath.asStringRef().empty()) {
        this->allStatistics.emplace_back(response.jobId,
                                         std::move(result.statistics));
      }
      this->indexPartPaths.emplace_back(
          std::move(response.result.emitIndex.indexPartPath));
      break;
    }
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
                    llvm_ext::format(recvError));
      // Keep going instead of exiting early for robustness.
    } else {
      // TODO(def: add-job-debug-helper): Add a simplified debug representation
      // for printing jobs for debugging.
      spdlog::debug("received response from worker {}", response.workerId);
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
  auto driverId = cliOptions.driverId.empty() ? fmt::format("{}", ::getpid())
                                              : cliOptions.driverId;
  size_t numWorkers = cliOptions.numWorkers;
  BOOST_TRY {
    Driver driver(driverId, DriverOptions(driverId, std::move(cliOptions)));
    driver.run();
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
