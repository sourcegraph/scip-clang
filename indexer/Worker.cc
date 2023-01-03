#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/YAMLTraits.h"

#include "indexer/CliOptions.h"
#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"
#include "indexer/Hash.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LLVMAdapter.h"
#include "indexer/Logging.h"
#include "indexer/Path.h"
#include "indexer/Worker.h"

namespace boost_ip = boost::interprocess;

namespace scip_clang {
namespace {

// Type representing the driver<->worker queues, as used by a worker.
struct MessageQueuePair {
  JsonIpcQueue driverToWorker;
  JsonIpcQueue workerToDriver;

  MessageQueuePair(std::string_view driverId, WorkerId workerId) {
    auto d2w = scip_clang::driverToWorkerQueueName(driverId, workerId);
    auto w2d = scip_clang::workerToDriverQueueName(driverId);
    this->driverToWorker =
        JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
            boost_ip::open_only, d2w.c_str()));
    this->workerToDriver =
        JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
            boost_ip::open_only, w2d.c_str()));
  }
};

// A type to keep track of the "transcript" (in Kythe terminology)
// of an #include being processed.
class HashValueBuilder {
public:
  using History = std::vector<std::string>;

private:
  // The hash value calculated so far for preprocessor effects.
  HashValue runningHash;

  // Optional field to track all the inputs that went into computing
  // a hash, meant for debugging. We buffer all the history for a
  // header instead of directly writing to a stream because if there
  // are multiple headers which match, having the output be interleaved
  // (due to the ~DAG nature of includes) would be confusing.
  std::unique_ptr<History> history;

public:
  HashValueBuilder(bool recordHistory)
      : runningHash(),
        history(recordHistory ? std::make_unique<History>() : nullptr) {}

  void mix(llvm::StringRef text) {
    this->runningHash.mix(reinterpret_cast<const uint8_t *>(text.data()),
                          text.size());
    if (this->history) {
      this->history->emplace_back(text.str());
    }
  }

  void mix(uint64_t v) {
    this->runningHash.mix(reinterpret_cast<const uint8_t *>(&v), sizeof(v));
    if (this->history) {
      this->history->emplace_back(fmt::format("{}", v));
    }
  }

  // Deliberately not named mix(..); we should avoid constructing temporary
  // strings which will be immediately be destroyed after the bytes are mixed
  // into the running hash value.
  void mixString(std::string &&text) {
    this->mix(llvm::StringRef(text));
  }

  std::pair<HashValue, std::unique_ptr<History>> finish() {
    return {this->runningHash, std::move(this->history)};
  }

  bool isRecordingHistory() {
    return (bool)this->history;
  }
};

#define MIX_WITH_KEY(__hash, __key_expr, __value)                   \
  {                                                                 \
    if (__hash.isRecordingHistory()) {                              \
      __hash.mixString(fmt::format("{}: {}", __key_expr, __value)); \
    } else {                                                        \
      __hash.mix(__value);                                          \
    }                                                               \
  }

struct HeaderInfoBuilder final {
  HashValueBuilder hashValueBuilder;
  const clang::FileID fileId;
};

class IndexerPreprocessorStack final {
  std::vector<HeaderInfoBuilder> state;

public:
  bool empty() {
    return this->state.empty();
  }
  HashValueBuilder &topHash() {
    ENFORCE(!this->empty());
    return this->state.back().hashValueBuilder;
  }
  std::optional<HeaderInfoBuilder> pop() {
    if (this->state.empty()) {
      return {};
    }
    auto info = std::move(this->state.back());
    this->state.pop_back();
    return info;
  }
  void push(HeaderInfoBuilder &&info) {
    this->state.emplace_back(std::move(info));
  }
};

struct PreprocessorHistoryRecorder {
  HeaderFilter filter;
  llvm::yaml::Output yamlStream;
};

struct IndexerPreprocessorOptions {
  AbsolutePath projectRoot;

  // Debugging-related
  PreprocessorHistoryRecorder *recorder;

  // Sort for deterministic output while running the preprocessor.
  bool ensureDeterminism;
};

struct PreprocessorHistoryEntry {
  llvm::StringRef path;
  HashValueBuilder::History &history;
};

} // namespace
} // namespace scip_clang

template <>
struct llvm::yaml::MappingTraits<scip_clang::PreprocessorHistoryEntry> {
  static void mapping(llvm::yaml::IO &io,
                      scip_clang::PreprocessorHistoryEntry &entry) {
    io.mapRequired("path", entry.path);
    io.mapRequired("history", entry.history);
  }
};

namespace scip_clang {
namespace {

class IndexerPPCallbacks final : public clang::PPCallbacks {
  const IndexerPreprocessorOptions &options;
  SemanticAnalysisJobResult &result;

  clang::SourceManager &sourceManager;
  IndexerPreprocessorStack stack;

  struct MultiHashValue {
    const HashValue hashValue;
    bool isMultiple;
  };
  // Headers which we've seen only expand in a single way.
  // The extra bit inside the MultiHashValue struct indicates
  // if should look in the finishedProcessingMulti map instead.
  absl::flat_hash_map<LLVMToAbslHashAdapter<clang::FileID>, MultiHashValue>
      finishedProcessing;
  // Headers which expand in at least 2 different ways.
  // The values have size() >= 2.
  absl::flat_hash_map<LLVMToAbslHashAdapter<clang::FileID>,
                      absl::flat_hash_set<HashValue>>
      finishedProcessingMulti;

public:
  IndexerPPCallbacks(clang::SourceManager &sourceManager,
                     const IndexerPreprocessorOptions &options,
                     SemanticAnalysisJobResult &result)
      : options(options), result(result), sourceManager(sourceManager), stack(),
        finishedProcessing(), finishedProcessingMulti() {}

  ~IndexerPPCallbacks() {
    auto getAbsPath = [&](clang::FileID fileId) -> std::optional<AbsolutePath> {
      ENFORCE(fileId.isValid(), "stored invalid FileID in map!");
      auto entry = this->sourceManager.getFileEntryForID(fileId);
      if (!entry) { // fileId represents an imaginary buffer; ignore those for
                    // indexing
        return {};
      }
      ENFORCE(entry, "missing entry for fileId stored in map");
      auto path = entry->tryGetRealPathName();
      if (path.empty()) {
        // TODO: attach some contextual information here!
        spdlog::warn("empty path for FileEntry");
        return {};
      }
      auto optAbsPath = AbsolutePath::tryFrom(path);
      if (!optAbsPath.has_value()) {
        // TODO: attach some contextual information
        spdlog::warn(
            "non-absolute path returned from tryGetRealPathName() = {}",
            scip_clang::toStringView(path));
      }
      return optAbsPath;
    };
    {
      auto &headerHashes = this->finishedProcessing;

      for (auto it = headerHashes.begin(), end = headerHashes.end(); it != end;
           ++it) {
        if (it->second.isMultiple) {
          continue;
        }
        auto hashValue = it->second.hashValue;
        auto fileId = it->first.data;
        if (auto optPath = getAbsPath(fileId)) {
          auto absPath = optPath.value();
          result.singlyExpandedHeaders.emplace_back(
              HeaderInfo{std::string(absPath.data()), hashValue});
        }
      }
      if (this->options.ensureDeterminism) {
        absl::c_sort(result.singlyExpandedHeaders);
      }
    }
    {
      auto &headerHashes = this->finishedProcessingMulti;
      std::vector<HashValue> buf;
      for (auto it = headerHashes.begin(), end = headerHashes.end(); it != end;
           ++it) {
        buf.clear();
        absl::c_move(it->second, std::back_inserter(buf));
        auto hashes = std::move(it->second);
        auto fileId = it->first.data;
        if (auto optPath = getAbsPath(fileId)) {
          auto absPath = optPath.value();
          result.multiplyExpandedHeaders.emplace_back(
              HeaderInfoMulti{std::string(absPath.data()), std::move(buf)});
        }
      }
      if (this->options.ensureDeterminism) {
        absl::c_sort(result.multiplyExpandedHeaders);
      }
    }
  }

  void enterInclude(bool recordHistory, clang::FileID enteredFileId) {
    this->stack.push(
        HeaderInfoBuilder{HashValueBuilder(recordHistory), enteredFileId});
  }

  std::optional<HashValue> exitInclude(clang::FileID fileId) {
    if (fileId.isInvalid()) {
      return {};
    }
    auto optHeaderInfo = this->stack.pop();
    ENFORCE(optHeaderInfo.has_value(),
            "missing matching enterInclude for exit");
    auto headerInfo = std::move(optHeaderInfo.value());
    bool fileIdMatchesTopOfStack = headerInfo.fileId == fileId;
    if (!fileIdMatchesTopOfStack) {
      ENFORCE(fileIdMatchesTopOfStack,
              "fileId mismatch:\ntop of stack: {}\nexitInclude: {}",
              debug::tryGetPath(this->sourceManager, headerInfo.fileId),
              debug::tryGetPath(this->sourceManager, fileId));
    }

    auto key = LLVMToAbslHashAdapter<clang::FileID>{headerInfo.fileId};
    auto it = this->finishedProcessing.find(key);
    auto [hashValue, history] = headerInfo.hashValueBuilder.finish();
    if (it == this->finishedProcessing.end()) {
      this->finishedProcessing.insert({key, MultiHashValue{hashValue, false}});
    } else if (it->second.isMultiple) {
      auto itMulti = this->finishedProcessingMulti.find(key);
      ENFORCE(itMulti != this->finishedProcessingMulti.end(),
              "isMultiple = true but key missing from finishedProcessingMulti");
      itMulti->second.insert(hashValue);
    } else if (it->second.hashValue != hashValue) {
      it->second.isMultiple = true;
      auto oldHash = it->second.hashValue;
      auto newHash = hashValue;
      auto [_, inserted] =
          this->finishedProcessingMulti.insert({key, {oldHash, newHash}});
      ENFORCE(inserted, "isMultiple = false, but key already present is "
                        "finishedProcessingMulti");
    }
    if (history) {
      ENFORCE(this->options.recorder,
              "Recorded history even though output stream is missing 🤔");
      auto &log = this->options.recorder->yamlStream;
      auto path = debug::tryGetPath(this->sourceManager, headerInfo.fileId);
      PreprocessorHistoryEntry entry{path, *history.get()};
      log << entry;
    }
    return hashValue;
  }

  // START overrides from PPCallbacks

  /// \param sourceLoc corresponds to the top of the newly entered file (if
  /// valid).
  /// \param reason describes
  ///   EnterFile is the reason when an #include is first expanded.
  ///   ExitFile is the reason when an #include finishes processing.
  ///     - With ExitFile, the sourceLoc points to the line _after_ the
  ///     #include.
  ///   RenameFile <- I'm not sure when this is triggered, maybe with #file?
  ///   SystemHeaderPragma is triggered on seeing \c{#pragma GCC system_header}.
  ///     - In this case, \p sourceLoc points to the location of the #pragma
  ///       and \p previousFileId is not valid.
  /// \param previousFileId corresponds to the previous file we were inside.
  ///   It may be invalid, for example, when entering the first file in the TU.
  ///   In some cases, \p sourceLoc can also correspond to something inside the
  ///   command-line; in that case, \p previousFileId may be invalid too.
  virtual void
  FileChanged(clang::SourceLocation sourceLoc,
              clang::PPCallbacks::FileChangeReason reason,
              clang::SrcMgr::CharacteristicKind fileType,
              clang::FileID previousFileId = clang::FileID()) override {
    using Reason = clang::PPCallbacks::FileChangeReason;
    switch (reason) {
    case Reason::SystemHeaderPragma:
    case Reason::RenameFile:
      return;
    case Reason::ExitFile: {
      auto optHash = this->exitInclude(previousFileId);
      if (!optHash || this->stack.empty()) {
        break;
      }
      MIX_WITH_KEY(
          this->stack.topHash(),
          fmt::format("hash for #include {}",
                      debug::tryGetPath(this->sourceManager, previousFileId)),
          optHash->rawValue);
      break;
    }
    case Reason::EnterFile: {
      if (!sourceLoc.isValid()) {
        break;
      }
      ENFORCE(sourceLoc.isFileID(), "EnterFile called on a non-FileID");
      auto enteredFileId = this->sourceManager.getFileID(sourceLoc);
      if (!enteredFileId.isValid()) {
        break;
      }
      bool recordHistory = false;
      if (this->options.recorder) {
        if (auto enteredFileEntry =
                this->sourceManager.getFileEntryForID(enteredFileId)) {
          auto path = enteredFileEntry->tryGetRealPathName();
          recordHistory =
              !path.empty() && this->options.recorder->filter.isMatch(path);
        }
      }
      this->enterInclude(recordHistory, enteredFileId);
      MIX_WITH_KEY(this->stack.topHash(), "self path",
                   debug::tryGetPath(this->sourceManager, enteredFileId));
      break;
    }
    }
  }

  // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/21):
  // Add overrides for
  // - MacroExpands
  // - MacroDefined
  // - MacroUndefined
  // - Defined
  // - Elif
  // - If
  // - Ifdef
  // - Ifndef
  // - InclusionDirective
  // Which adjust the transcript.

  // END overrides from PPCallbacks
};

class IndexerASTVisitor : public clang::RecursiveASTVisitor<IndexerASTVisitor> {
  using Base = RecursiveASTVisitor;

  // For the various hierarchies, see clang/Basic/.*.td files
  // https://sourcegraph.com/search?q=context:global+repo:llvm/llvm-project%24+file:clang/Basic/.*.td&patternType=standard&sm=1&groupBy=repo
};

class IndexerASTConsumer : public clang::SemaConsumer {
  clang::Sema *sema;

public:
  IndexerASTConsumer(clang::CompilerInstance &compilerInstance,
                     llvm::StringRef filepath) {}

  void HandleTranslationUnit(clang::ASTContext &astContext) override {
    IndexerASTVisitor visitor{};
    visitor.VisitTranslationUnitDecl(astContext.getTranslationUnitDecl());
  }

  void InitializeSema(clang::Sema &S) override {
    this->sema = &S;
  }

  void ForgetSema() override {
    this->sema = nullptr;
  }
};

class IndexerFrontendAction : public clang::ASTFrontendAction {
  const IndexerPreprocessorOptions &options;
  SemanticAnalysisJobResult &result;
  // ^ These fields are just for passing down information
  // down to the IndexerPPCallbacks value.
public:
  IndexerFrontendAction(const IndexerPreprocessorOptions &options,
                        SemanticAnalysisJobResult &result)
      : options(options), result(result) {}

  bool usesPreprocessorOnly() const override {
    return false;
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &compilerInstance,
                    llvm::StringRef filepath) override {
    auto &preprocessor = compilerInstance.getPreprocessor();
    preprocessor.addPPCallbacks(std::make_unique<IndexerPPCallbacks>(
        compilerInstance.getSourceManager(), options, result));
    return std::make_unique<IndexerASTConsumer>(compilerInstance, filepath);
  }
};

class IndexerFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
  const IndexerPreprocessorOptions &options;
  SemanticAnalysisJobResult &result;

public:
  IndexerFrontendActionFactory(const IndexerPreprocessorOptions &options,
                               SemanticAnalysisJobResult &result)
      : options(options), result(result) {}

  virtual std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<IndexerFrontendAction>(options, result);
  }
};

class IndexerDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    // Just dropping all diagnostics on the floor for now.
    // FIXME(def: surface-diagnostics)
  }
};

} // namespace

struct WorkerOptions {
  std::chrono::seconds receiveTimeout;
  spdlog::level::level_enum logLevel;
  bool deterministic;
  std::string recordHistoryRegex;
  std::string preprocessorHistoryLogPath;
  std::string driverId;
  uint64_t workerId;

  WorkerOptions(const CliOptions &cliOptions)
      : receiveTimeout(cliOptions.receiveTimeout),
        logLevel(cliOptions.logLevel), deterministic(cliOptions.deterministic),
        recordHistoryRegex(cliOptions.preprocessorRecordHistoryFilterRegex),
        preprocessorHistoryLogPath(cliOptions.preprocessorHistoryLogPath),
        driverId(cliOptions.driverId), workerId(cliOptions.workerId) {}
};

class Worker {
  WorkerOptions options;
  MessageQueuePair messageQueues;

  /// The llvm::yaml::Output object doesn't take ownership
  /// of the underlying stream, so hold it separately.
  ///
  /// The stream is wrapped in an extra unique_ptr because
  /// \c llvm::raw_fd_ostream doesn't have a move constructor
  /// for some reason.
  std::optional<std::pair<std::unique_ptr<llvm::raw_fd_ostream>,
                          PreprocessorHistoryRecorder>>
      recorder;

public:
  Worker(WorkerOptions &&options)
      : options(std::move(options)),
        messageQueues(
            MessageQueuePair(this->options.driverId, this->options.workerId)),
        recorder() {
    HeaderFilter filter(std::string(this->options.recordHistoryRegex));
    if (filter.isIdentity()) {
      return;
    }
    ENFORCE(!this->options.preprocessorHistoryLogPath.empty());
    std::error_code error;
    auto ostream = std::make_unique<llvm::raw_fd_ostream>(
        llvm::StringRef(this->options.preprocessorHistoryLogPath), error);
    if (error) {
      spdlog::error(
          "failed to open file for recording preprocessor history at '{}'",
          this->options.preprocessorHistoryLogPath);
      spdlog::error("I/O error: {}", error.message());
      std::exit(EXIT_FAILURE);
    }
    llvm::yaml::Output yamlStream(*ostream.get());
    PreprocessorHistoryRecorder recorder{std::move(filter),
                                         std::move(yamlStream)};
    this->recorder.emplace(
        std::make_pair(std::move(ostream), std::move(recorder)));
  }

  void performSemanticAnalysis(SemanticAnalysisJobDetails &&job,
                               SemanticAnalysisJobResult &result) {
    clang::FileSystemOptions fileSystemOptions;
    fileSystemOptions.WorkingDir = std::move(job.command.Directory);

    llvm::IntrusiveRefCntPtr<clang::FileManager> fileManager(
        new clang::FileManager(fileSystemOptions, nullptr));

    auto args = std::move(job.command.CommandLine);
    args.push_back("-fsyntax-only");   // Only type-checking, no codegen.
    args.push_back("-Wno-everything"); // Warnings aren't helpful.
    // Should we add a CLI flag to pass through extra arguments here?

    auto projectRoot = std::filesystem::current_path().string();
    IndexerPreprocessorOptions options{
        AbsolutePath::tryFrom(std::string_view(projectRoot)).value(),
        this->recorder.has_value() ? &this->recorder->second : nullptr,
        this->options.deterministic};
    auto frontendActionFactory = IndexerFrontendActionFactory(options, result);

    clang::tooling::ToolInvocation Invocation(
        std::move(args), &frontendActionFactory, fileManager.get(),
        std::make_shared<clang::PCHContainerOperations>());

    IndexerDiagnosticConsumer diagnosticConsumer;
    Invocation.setDiagnosticConsumer(&diagnosticConsumer);

    {
      LogTimerRAII timer(
          fmt::format("invocation for {}", job.command.Filename));
      bool ranSuccessfully = Invocation.run();
      // FIXME(def: delay-ast-traversal): Right now, IIUC, this will run the
      // pre-processor, and then run the AST traversal directly. However,
      // after the preprocessor is done, we want to perform some extra logic
      // which sends a message to the driver, gets back some information,
      // which then customizes the AST traversal. I'm not 100% sure on the
      // best way to do this, but one idea is to pass down a callback
      // (call it 'setTraversalOptions') to IndexerPPCallbacks.
      // 'setTraversalOptions' can be invoked during 'EndOfMainFile' which
      // is the last overriden method to be called. Later, that state
      // can be read by IndexerASTConsumer in InitializeSema.

      (void)ranSuccessfully; // FIXME(ref: surface-diagnostics)
    }
  }

  void processRequest(IndexJobRequest &&request, IndexJobResult &result) {
    result.kind = request.job.kind;
    switch (request.job.kind) {
    case IndexJob::Kind::EmitIndex:
      result.emitIndex = EmitIndexJobResult{"lol"};
      break;
    case IndexJob::Kind::SemanticAnalysis:
      this->performSemanticAnalysis(std::move(request.job.semanticAnalysis),
                                    result.semanticAnalysis);
      break;
    }
  }

  void flushStreams() {
    if (this->recorder) {
      this->recorder->first->flush();
    }
  }

  void run() {
    auto &mq = this->messageQueues;
    while (true) {
      IndexJobRequest request{};
      auto recvError =
          mq.driverToWorker.timedReceive(request, this->options.receiveTimeout);
      if (recvError.isA<TimeoutError>()) {
        spdlog::error(
            "timeout in worker; is the driver dead?... shutting down");
        break;
      }
      if (recvError) {
        spdlog::error("received malformed message: {}",
                      scip_clang::formatLLVM(recvError));
        continue;
      }
      if (request.id == JobId::Shutdown()) {
        spdlog::debug("shutting down");
        break;
      }
      auto requestId = request.id;
      IndexJobResult result;
      this->processRequest(std::move(request), result);
      mq.workerToDriver.send(IndexJobResponse{this->options.workerId, requestId,
                                              std::move(result)});
      this->flushStreams();
    }
  }
};

int workerMain(CliOptions &&cliOptions) {
  BOOST_TRY {
    Worker worker((WorkerOptions(cliOptions)));
    worker.run();
  }
  BOOST_CATCH(boost_ip::interprocess_exception & ex) {
    // Don't delete queue from worker; let driver handle that.
    spdlog::error("worker failed {}; exiting from throw!\n", ex.what());
    return 1;
  }
  BOOST_CATCH_END
  spdlog::debug("exiting cleanly");
  return 0;
}

} // namespace scip_clang
