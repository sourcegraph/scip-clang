#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "boost/interprocess/exceptions.hpp"
#include "perfetto/perfetto.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/AstConsumer.h"
#include "indexer/CliOptions.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/IpcMessages.h"
#include "indexer/Logging.h"
#include "indexer/Preprocessing.h"
#include "indexer/Statistics.h"
#include "indexer/Tracing.h"
#include "indexer/Worker.h"

namespace scip_clang {

namespace {

class IndexerFrontendAction : public clang::ASTFrontendAction {
  const IndexerPreprocessorOptions &preprocessorOptions;
  const IndexerAstConsumerOptions &astConsumerOptions;
  TuIndexingOutput &tuIndexingOutput;

public:
  IndexerFrontendAction(const IndexerPreprocessorOptions &preprocessorOptions,
                        const IndexerAstConsumerOptions &astConsumerOptions,
                        TuIndexingOutput &tuIndexingOutput)
      : preprocessorOptions(preprocessorOptions),
        astConsumerOptions(astConsumerOptions),
        tuIndexingOutput(tuIndexingOutput) {}

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &compilerInstance,
                    llvm::StringRef filepath) override {
    compilerInstance.getLangOpts().CommentOpts.ParseAllComments = true;
    auto &preprocessor = compilerInstance.getPreprocessor();
    auto callbacks = std::make_unique<IndexerPreprocessorWrapper>(
        compilerInstance.getSourceManager(), this->preprocessorOptions,
        PreprocessorDebugContext{filepath.str()});
    // SAFETY: See NOTE(ref: preprocessor-traversal-ordering)
    // Ideally, we'd use a shared_ptr, but addPPCallbacks needs a unique_ptr.
    auto preprocessorWrapper = callbacks.get();
    preprocessor.addPPCallbacks(std::move(callbacks));
    return std::make_unique<IndexerAstConsumer>(
        compilerInstance, filepath, this->astConsumerOptions,
        preprocessorWrapper, this->tuIndexingOutput);
  }
};

class IndexerFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
  const IndexerPreprocessorOptions &preprocessorOptions;
  const IndexerAstConsumerOptions &astConsumerOptions;
  TuIndexingOutput &tuIndexingOutput;

public:
  IndexerFrontendActionFactory(
      const IndexerPreprocessorOptions &preprocessorOptions,
      const IndexerAstConsumerOptions &astConsumerOptions,
      TuIndexingOutput &tuIndexingOutput)
      : preprocessorOptions(preprocessorOptions),
        astConsumerOptions(astConsumerOptions),
        tuIndexingOutput(tuIndexingOutput) {}

  virtual std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<IndexerFrontendAction>(this->preprocessorOptions,
                                                   this->astConsumerOptions,
                                                   this->tuIndexingOutput);
  }
};

class SuppressDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level,
                        const clang::Diagnostic &) override {}
};

} // namespace

WorkerOptions WorkerOptions::fromCliOptions(const CliOptions &cliOptions) {
  RootPath projectRootPath{
      AbsolutePath{std::filesystem::current_path().string()},
      RootKind::Project};
  WorkerMode mode;
  IpcOptions ipcOptions;
  StdPath compdbPath{};
  StdPath indexOutputPath{};
  StdPath statsFilePath{};
  if (cliOptions.workerMode == "ipc") {
    mode = WorkerMode::Ipc;
    ipcOptions = cliOptions.ipcOptions();
  } else if (cliOptions.workerMode == "compdb") {
    mode = WorkerMode::Compdb;
    compdbPath = StdPath(cliOptions.compdbPath);
    indexOutputPath = StdPath(cliOptions.indexOutputPath);
    statsFilePath = StdPath(cliOptions.statsFilePath);
  } else {
    ENFORCE(cliOptions.workerMode == "testing");
    mode = WorkerMode::Testing;
  }
  return WorkerOptions{projectRootPath,
                       mode,
                       ipcOptions,
                       compdbPath,
                       indexOutputPath,
                       statsFilePath,
                       cliOptions.packageMapPath,
                       cliOptions.showCompilerDiagnostics,
                       cliOptions.showProgress,
                       cliOptions.logLevel,
                       cliOptions.deterministic,
                       cliOptions.measureStatistics,
                       PreprocessorHistoryRecordingOptions{
                           cliOptions.preprocessorRecordHistoryFilterRegex,
                           cliOptions.preprocessorHistoryLogPath, false, ""},
                       cliOptions.temporaryOutputDir,
                       cliOptions.workerFault};
}

Worker::Worker(WorkerOptions &&options)
    : options(std::move(options)),
      packageMap(this->options.projectRootPath, this->options.packageMapPath,
                 this->options.mode == WorkerMode::Testing),
      messageQueues(), compileCommands(), commandIndex(0), recorder(),
      statistics() {
  switch (this->options.mode) {
  case WorkerMode::Ipc:
    this->messageQueues = std::make_unique<MessageQueuePair>(
        MessageQueuePair::forWorker(this->options.ipcOptions));
    break;
  case WorkerMode::Compdb: {
    auto compdbFile = compdb::File::openAndExitOnErrors(
        this->options.compdbPath,
        compdb::ValidationOptions{.checkDirectoryPathsAreAbsolute = true});
    compdb::ResumableParser parser{};
    parser.initialize(compdbFile,
                      compdb::ParseOptions::create(
                          /*refillCount*/ std::numeric_limits<size_t>::max()));
    parser.parseMore(this->compileCommands);
    std::fclose(compdbFile.file);
    break;
  }
  case WorkerMode::Testing:
    break;
  }

  // All initialization unrelated to preprocessor recording should be
  // completed here.

  auto &recordingOptions = this->options.recordingOptions;
  HeaderFilter filter(std::string(recordingOptions.filterRegex));
  if (filter.isIdentity()) {
    return;
  }
  ENFORCE(!recordingOptions.preprocessorHistoryLogPath.empty());
  std::error_code error;
  auto ostream = std::make_unique<llvm::raw_fd_ostream>(
      llvm::StringRef(recordingOptions.preprocessorHistoryLogPath), error);
  if (error) {
    spdlog::error("failed to open preprocessor history recording file at '{}'; "
                  "I/O error: {}",
                  recordingOptions.preprocessorHistoryLogPath, error.message());
    std::exit(EXIT_FAILURE);
  }
  llvm::yaml::Output yamlStream(*ostream.get());
  bool preferRelativePaths = recordingOptions.preferRelativePaths;
  std::string rootPath = recordingOptions.rootPath;
  PreprocessorHistoryRecorder recorder{
      std::move(filter), std::move(yamlStream),
      [preferRelativePaths, rootPath](llvm::StringRef sref) -> llvm::StringRef {
        if (preferRelativePaths && sref.starts_with(rootPath)) {
          return sref.slice(rootPath.size(), sref.size());
        }
        return sref;
      }};
  this->recorder.emplace(
      std::make_pair(std::move(ostream), std::move(recorder)));
}

const IpcOptions &Worker::ipcOptions() const {
  return this->options.ipcOptions;
}

void Worker::processTranslationUnit(SemanticAnalysisJobDetails &&job,
                                    WorkerCallback workerCallback,
                                    TuIndexingOutput &tuIndexingOutput) {
  auto optPathRef =
      AbsolutePathRef::tryFrom(std::string_view(job.command.workingDirectory));
  ENFORCE(optPathRef.has_value()); // See NOTE(ref: directory-field-is-absolute)
  RootPath buildRootPath{AbsolutePath{optPathRef.value()}, RootKind::Build};

  clang::FileSystemOptions fileSystemOptions;
  fileSystemOptions.WorkingDir = job.command.workingDirectory;

  llvm::IntrusiveRefCntPtr<clang::FileManager> fileManager(
      new clang::FileManager(fileSystemOptions, nullptr));

  auto args = std::move(job.command.arguments);
  args.push_back("-fsyntax-only");   // Only type-checking, no codegen.
  args.push_back("-Wno-everything"); // Warnings aren't helpful.
  args.push_back("-working-directory");
  args.push_back(fileSystemOptions.WorkingDir);
  args.push_back("-ferror-limit=0"); // TODO: Add test for this
  // clang-format off
  // TODO(def: flag-passthrough, issue: https://github.com/sourcegraph/scip-clang/issues/23)
  // Support passing through CLI flags to Clang, similar to --extra-arg in lsif-clang
  // clang-format on

  IndexerPreprocessorOptions preprocessorOptions{
      this->options.projectRootPath,
      this->recorder.has_value() ? &this->recorder->second : nullptr,
      this->options.deterministic};
  IndexerAstConsumerOptions astConsumerOptions{
      this->options.projectRootPath, buildRootPath, std::move(workerCallback),
      this->options.deterministic, this->packageMap};
  auto frontendActionFactory = IndexerFrontendActionFactory(
      preprocessorOptions, astConsumerOptions, tuIndexingOutput);

  clang::tooling::ToolInvocation invocation(
      std::move(args), &frontendActionFactory, fileManager.get(),
      std::make_shared<clang::PCHContainerOperations>());

  SuppressDiagnosticConsumer suppressDiagnostics;
  if (!this->options.showCompilerDiagnostics) {
    invocation.setDiagnosticConsumer(&suppressDiagnostics);
  }

  {
    LogTimerRAII timer(fmt::format("invocation for {}", job.command.filePath));
    bool ranSuccessfully = invocation.run();
    (void)ranSuccessfully;
  }
}

void Worker::emitIndex(google::protobuf::Message &&message,
                       const StdPath &outputPath) {
  std::ofstream outputStream(outputPath, std::ios_base::out
                                             | std::ios_base::binary
                                             | std::ios_base::trunc);
  if (outputStream.fail()) {
    spdlog::warn("failed to open file to write shard at '{}' ({})",
                 outputPath.c_str(), std::strerror(errno));
    std::exit(EXIT_FAILURE);
  }
  message.SerializeToOstream(&outputStream);
}

void Worker::sendResult(JobId requestId, IndexJobResult &&result) {
  ENFORCE(this->options.mode == WorkerMode::Ipc);
  spdlog::debug("sending result for {}", requestId);
  auto sendError = this->messageQueues->workerToDriver.send(IndexJobResponse{
      this->ipcOptions().workerId, requestId, std::move(result)});
  if (sendError.has_value()) {
    spdlog::warn(
        "exiting after failing to send response from worker to driver: {}",
        sendError->what());
    std::exit(EXIT_FAILURE);
  }
  this->flushStreams();
}

Worker::ReceiveStatus Worker::sendRequestAndReceive(
    JobId semaRequestId, std::string_view tuMainFilePath,
    SemanticAnalysisJobResult &&semaResult, IndexJobRequest &emitIndexRequest) {
  this->sendResult(semaRequestId,
                   IndexJobResult{IndexJob::Kind::SemanticAnalysis,
                                  std::move(semaResult), EmitIndexJobResult{}});
  auto status = this->waitForRequest(emitIndexRequest);
  if (status != ReceiveStatus::OK) {
    return status;
  }
  if (emitIndexRequest.id == JobId::Shutdown()) {
    spdlog::warn("expected EmitIndex request for '{}' but got Shutdown signal",
                 tuMainFilePath);
    std::exit(EXIT_FAILURE);
  }
  return status;
}

Worker::ReceiveStatus Worker::processTranslationUnitAndRespond(
    IndexJobRequest &&semanticAnalysisRequest) {
  TRACE_EVENT_BEGIN(
      tracing::indexing, "worker.semanticAnalysis",
      perfetto::Flow::Global(semanticAnalysisRequest.id.traceId()));
  ManualTimer indexingTimer{};
  indexingTimer.start();

  SemanticAnalysisJobResult semaResult{};
  auto semaRequestId = semanticAnalysisRequest.id;
  auto tuMainFilePath =
      semanticAnalysisRequest.job.semanticAnalysis.command.filePath;
  Worker::ReceiveStatus innerStatus;
  JobId emitIndexRequestId;
  unsigned callbackInvoked = 0;

  auto callback =
      [this, semaRequestId, &innerStatus, &emitIndexRequestId, &tuMainFilePath,
       &callbackInvoked](SemanticAnalysisJobResult &&semaResult,
                         EmitIndexJobDetails &emitIndexDetails) -> bool {
    TRACE_EVENT_END(tracing::indexing);
    callbackInvoked++;
    if (this->options.mode == WorkerMode::Compdb) {
      for (auto &fileInfo : semaResult.wellBehavedFiles) {
        emitIndexDetails.filesToBeIndexed.emplace_back(std::move(fileInfo));
      }
      for (auto &fileInfoMulti : semaResult.illBehavedFiles) {
        for (auto &hashValue : fileInfoMulti.hashValues) {
          emitIndexDetails.filesToBeIndexed.emplace_back(
              PreprocessedFileInfo{fileInfoMulti.path, hashValue});
        }
      }
      return true;
    }
    IndexJobRequest emitIndexRequest{};
    innerStatus = this->sendRequestAndReceive(
        semaRequestId, tuMainFilePath, std::move(semaResult), emitIndexRequest);
    if (innerStatus != ReceiveStatus::OK) {
      return false;
    }
    TRACE_EVENT_BEGIN(tracing::indexing, "worker.emitIndex",
                      perfetto::Flow::Global(emitIndexRequest.id.traceId()));
    ENFORCE(emitIndexRequest.job.kind == IndexJob::Kind::EmitIndex,
            "expected EmitIndex request for '{}' but got SemanticAnalysis "
            "request for '{}'",
            tuMainFilePath,
            emitIndexRequest.job.semanticAnalysis.command.filePath);
    emitIndexDetails = std::move(emitIndexRequest.job.emitIndex);
    emitIndexRequestId = emitIndexRequest.id;
    return true;
  };
  TuIndexingOutput tuIndexingOutput{};
  auto &semaDetails = semanticAnalysisRequest.job.semanticAnalysis;
  // deliberate copy
  std::vector<std::string> commandLine = semaDetails.command.arguments;

  scip_clang::exceptionContext =
      fmt::format("processing {}", semaDetails.command.filePath);
  this->processTranslationUnit(std::move(semaDetails), callback,
                               tuIndexingOutput);
  scip_clang::exceptionContext = "";

  if (callbackInvoked == 0) {
    spdlog::warn("failed to index '{}' as semantic analysis didn't run; retry "
                 "running scip-clang with --show-compiler-diagnostics",
                 tuMainFilePath);
    for (auto &arg : commandLine) {
      if (arg.starts_with("$(") && arg.ends_with(")")) {
        spdlog::info(
            "hint: found unexpanded '{}' in command line arguments for '{}'",
            arg, tuMainFilePath);
      }
    }
    // TODO: Add a different result kind indicating semantic analysis failure.
    // Keep going with no-ops so as to not create errors.
    IndexJobRequest emitIndexRequest{};
    innerStatus = this->sendRequestAndReceive(semaRequestId, tuMainFilePath,
                                              SemanticAnalysisJobResult{},
                                              emitIndexRequest);
    emitIndexRequestId = emitIndexRequest.id;
  } else {
    ENFORCE(callbackInvoked == 1,
            "callbackInvoked = {} for TU with main file '{}'", callbackInvoked,
            tuMainFilePath);
  }
  if (innerStatus != ReceiveStatus::OK) {
    return innerStatus;
  }

  auto stopTimer = [&]() -> void {
    indexingTimer.stop();
    this->statistics.totalTimeMicros =
        uint64_t(indexingTimer.value<std::chrono::microseconds>());
    TRACE_EVENT_END(tracing::indexing);
  };

  if (this->options.mode == WorkerMode::Compdb) {
    StdPath outputPath = this->options.indexOutputPath;
    this->emitIndex(std::move(tuIndexingOutput.docsAndExternals), outputPath);
    stopTimer();
    if (!this->options.statsFilePath.empty()) {
      StatsEntry::emitAll({StatsEntry{tuMainFilePath, this->statistics}},
                          this->options.statsFilePath.c_str());
    }
    return ReceiveStatus::OK;
  }

  StdPath prefix = this->options.temporaryOutputDir
                   / ShardPaths::prefix(emitIndexRequestId.taskId(),
                                        this->ipcOptions().workerId);
  StdPath docsAndExternalsOutputPath = prefix;
  docsAndExternalsOutputPath.concat("-docs_and_externals.shard.scip");
  StdPath forwardDeclsOutputPath = prefix;
  forwardDeclsOutputPath.concat("-forward_decls.shard.scip");
  this->emitIndex(std::move(tuIndexingOutput.docsAndExternals),
                  docsAndExternalsOutputPath);
  this->emitIndex(std::move(tuIndexingOutput.forwardDecls),
                  forwardDeclsOutputPath);
  stopTimer();

  EmitIndexJobResult emitIndexResult{
      this->statistics,
      ShardPaths{AbsolutePath{docsAndExternalsOutputPath.string()},
                 AbsolutePath{forwardDeclsOutputPath.string()}}};

  this->sendResult(emitIndexRequestId,
                   IndexJobResult{IndexJob::Kind::EmitIndex,
                                  SemanticAnalysisJobResult{},
                                  std::move(emitIndexResult)});
  return Worker::ReceiveStatus::OK;
}

void Worker::flushStreams() {
  if (this->recorder) {
    this->recorder->first->flush();
  }
}

[[clang::optnone]] __attribute__((no_sanitize("undefined"))) void
crashWorker() {
  const char *p = nullptr;
  asm volatile("" ::: "memory");
  spdlog::warn("about to crash");
  char x = *p;
  (void)x;
}

void Worker::triggerFaultIfApplicable() const {
  auto &fault = this->options.workerFault;
  if (fault.empty()) {
    return;
  }
  if (fault == "crash") {
    crashWorker();
  } else if (fault == "sleep") {
    spdlog::warn("about to sleep");
    std::this_thread::sleep_for(this->ipcOptions().receiveTimeout * 10);
  } else if (fault == "spin") {
    std::error_code ec;
    llvm::raw_fd_ostream devNull("/dev/null", ec);
    spdlog::warn("about to spin");
    // Q: Better way to spin other than using the Collatz conjecture?
    for (uint64_t i = 1; i != UINT64_MAX; ++i) {
      uint64_t j = i;
      while (j > 1) {
        j = (j % 2 == 0) ? (j / 2) : (j * 3 + 1);
      }
      devNull << j;
    }
  } else {
    spdlog::error("Unknown fault kind {}", fault);
    std::exit(EXIT_FAILURE);
  }
}

Worker::ReceiveStatus Worker::waitForRequest(IndexJobRequest &request) {
  TRACE_EVENT(tracing::ipc, "Worker::waitForRequest");

  using Status = Worker::ReceiveStatus;

  if (this->options.mode == WorkerMode::Compdb) {
    if (this->commandIndex >= this->compileCommands.size()) {
      return Status::Shutdown;
    }
    request.id = JobId::newTask(this->commandIndex);
    auto &command = this->compileCommands[this->commandIndex];
    ++this->commandIndex;
    request.job =
        IndexJob{IndexJob::Kind::SemanticAnalysis,
                 SemanticAnalysisJobDetails{command}, EmitIndexJobDetails{}};
    return Status::OK;
  }

  ENFORCE(this->options.mode == WorkerMode::Ipc);
  auto recvError = this->messageQueues->driverToWorker.timedReceive(
      request, this->ipcOptions().receiveTimeout);
  if (recvError.isA<TimeoutError>()) {
    spdlog::error("timeout in worker; is the driver dead?... shutting down");
    return Status::DriverTimeout;
  }
  if (recvError) {
    spdlog::error("received malformed message: {}",
                  llvm_ext::format(recvError));
    return Status::MalformedMessage;
  }
  if (request.id == JobId::Shutdown()) {
    spdlog::debug("shutting down");
    return Status::Shutdown;
  }
  spdlog::debug("received job {}", request.id);
  this->triggerFaultIfApplicable();
  return Status::OK;
}

void Worker::run() {
  ENFORCE(this->options.mode != WorkerMode::Testing,
          "tests typically call method individually");
  [&]() {
    while (true) {
      IndexJobRequest request{};
      using Status = Worker::ReceiveStatus;
#define CHECK_STATUS(_expr)      \
  switch (_expr) {               \
  case Status::Shutdown:         \
  case Status::DriverTimeout:    \
    return;                      \
  case Status::MalformedMessage: \
    continue;                    \
  case Status::OK:               \
    break;                       \
  }
      CHECK_STATUS(this->waitForRequest(request));
      ENFORCE(request.job.kind == IndexJob::Kind::SemanticAnalysis);
      CHECK_STATUS(this->processTranslationUnitAndRespond(std::move(request)));
    }
  }();
}

int workerMain(CliOptions &&cliOptions) {
  BOOST_TRY {
    Worker worker((WorkerOptions::fromCliOptions(cliOptions)));
    worker.run();
  }
  BOOST_CATCH(boost::interprocess::interprocess_exception & ex) {
    // Don't delete queue from worker; let driver handle that.
    spdlog::error("worker failed {}; exiting from throw!\n", ex.what());
    return 1;
  }
  BOOST_CATCH_END
  spdlog::debug("exiting cleanly");
  return 0;
}

} // namespace scip_clang
