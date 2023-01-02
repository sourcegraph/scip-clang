#include <chrono>
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

class HashValueBuilder {
  // The hash value calculated so far for preprocessor effects.
  HashValue runningHash;
  // Optional field to track all the inputs that went into computing
  // a hash, meant for debugging.
  std::unique_ptr<std::vector<std::string>> history;

public:
  HashValueBuilder(bool recordHistory)
      : runningHash(),
        history(recordHistory ? std::make_unique<std::vector<std::string>>()
                              : nullptr) {}

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

  void mixString(std::string &&text) {
    this->mix(llvm::StringRef(text));
  }

  HashValue finish() const {
    return this->runningHash;
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
  const clang::FileID fileId;
  HashValueBuilder hashValueBuilder;
};

struct MultiHashValue {
  const HashValue hashValue;
  bool isMultiple;
};

class IndexerPPStack final {
public:
  std::vector<HeaderInfoBuilder> state;
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
  bool empty() {
    return this->state.empty();
  }
  HashValueBuilder &topHash() {
    ENFORCE(!this->empty());
    return this->state.back().hashValueBuilder;
  }

  void enterInclude(clang::FileID fileId, bool recordHistory) {
    this->state.emplace_back(
        HeaderInfoBuilder{fileId, HashValueBuilder(recordHistory)});
  }
  std::optional<HashValue>
  exitInclude(clang::FileID fileId, const clang::SourceManager &sourceManager) {
    if (fileId.isInvalid()) {
      return {};
    }
    ENFORCE(!this->state.empty(), "missing matching enterInclude for exit");
    auto headerInfo = std::move(this->state.back());
    this->state.pop_back();
    bool fileIdMatchesTopOfStack = headerInfo.fileId == fileId;
    if (!fileIdMatchesTopOfStack) {
      ENFORCE(fileIdMatchesTopOfStack,
              "fileId mismatch:\ntop of stack: {}\nexitInclude: {}",
              debug::tryGetPath(sourceManager, headerInfo.fileId),
              debug::tryGetPath(sourceManager, fileId));
    }

    auto key = LLVMToAbslHashAdapter<clang::FileID>{headerInfo.fileId};
    auto it = this->finishedProcessing.find(key);
    auto hashValue = headerInfo.hashValueBuilder.finish();
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
    return hashValue;
  }
};

struct IndexerPPConfig {
  AbsolutePath projectRoot;

  // Debugging-related
  // Option to turn on history recording for header transcripts.
  HeaderFilter recordHistoryFilter;

  // Sort for deterministic output while running the preprocessor.
  bool ensureDeterminism;
};

class IndexerPPCallbacks final : public clang::PPCallbacks {
  clang::SourceManager &sourceManager;
  IndexerPPStack stack;

  const IndexerPPConfig &config;
  SemanticAnalysisJobResult &result;

public:
  IndexerPPCallbacks(clang::SourceManager &sourceManager,
                     const IndexerPPConfig &config,
                     SemanticAnalysisJobResult &result)
      : sourceManager(sourceManager), stack(), config(config), result(result) {}

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
      auto &headerHashes = this->stack.finishedProcessing;

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
      if (this->config.ensureDeterminism) {
        absl::c_sort(result.singlyExpandedHeaders);
      }
    }
    {
      auto &headerHashes = this->stack.finishedProcessingMulti;
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
      if (this->config.ensureDeterminism) {
        absl::c_sort(result.multiplyExpandedHeaders);
      }
    }
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
    // 1. For every header, store a list of locations where it is consumed.
    using Reason = clang::PPCallbacks::FileChangeReason;
    switch (reason) {
    case Reason::SystemHeaderPragma:
    case Reason::RenameFile:
      return;
    case Reason::ExitFile: {
      if (auto optHash =
              stack.exitInclude(previousFileId, this->sourceManager)) {
        if (!this->stack.empty()) {
          MIX_WITH_KEY(this->stack.topHash(),
                       fmt::format("hash for #include {}",
                                   debug::tryGetPath(this->sourceManager,
                                                     previousFileId)),
                       optHash->rawValue);
        }
      }
      break;
    }
    case Reason::EnterFile:
      if (sourceLoc.isValid()) {
        // TODO(ref: add-enforce): Enforce that we have a FileId here.
        if (sourceLoc.isFileID()) {
          auto enteredFileId = this->sourceManager.getFileID(sourceLoc);
          if (enteredFileId.isValid()) {
            ENFORCE(this->sourceManager.getSLocEntry(enteredFileId).isFile(),
                    "sourceLoc.isFileID() is true, but sloc isFile() is false");
            bool recordHistory = false;
            if (!this->config.recordHistoryFilter.isIdentity()) {
              if (auto enteredFileEntry =
                      this->sourceManager.getFileEntryForID(enteredFileId)) {
                auto path = enteredFileEntry->tryGetRealPathName();
                recordHistory = !path.empty()
                                && this->config.recordHistoryFilter.isMatch(
                                    toStringView(path));
              }
            }
            this->stack.enterInclude(enteredFileId, recordHistory);
            MIX_WITH_KEY(this->stack.topHash(), "self path",
                         debug::tryGetPath(this->sourceManager, enteredFileId));
          }
        }
      }
      break;
    }
  }

  // FIXME: Add overrides for
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
  const IndexerPPConfig &config;
  SemanticAnalysisJobResult &result;
  // ^ These fields are just for passing down information
  // down to the IndexerPPCallbacks value.
public:
  IndexerFrontendAction(const IndexerPPConfig &config,
                        SemanticAnalysisJobResult &result)
      : config(config), result(result) {}

  bool usesPreprocessorOnly() const override {
    return false;
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &compilerInstance,
                    llvm::StringRef filepath) override {
    auto &preprocessor = compilerInstance.getPreprocessor();
    preprocessor.addPPCallbacks(std::make_unique<IndexerPPCallbacks>(
        compilerInstance.getSourceManager(), config, result));
    return std::make_unique<IndexerASTConsumer>(compilerInstance, filepath);
  }
};

class IndexerFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
  const IndexerPPConfig &config;
  SemanticAnalysisJobResult &result;

public:
  IndexerFrontendActionFactory(const IndexerPPConfig &config,
                               SemanticAnalysisJobResult &result)
      : config(config), result(result) {}

  virtual std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<IndexerFrontendAction>(config, result);
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

static SemanticAnalysisJobResult
performSemanticAnalysis(SemanticAnalysisJobDetails &&job) {
  clang::FileSystemOptions fileSystemOptions;
  fileSystemOptions.WorkingDir = std::move(job.command.Directory);

  llvm::IntrusiveRefCntPtr<clang::FileManager> fileManager(
      new clang::FileManager(fileSystemOptions, nullptr));

  auto args = std::move(job.command.CommandLine);
  args.push_back("-fsyntax-only");   // Only type-checking, no codegen.
  args.push_back("-Wno-everything"); // Warnings aren't helpful.
  // Should we add a CLI flag to pass through extra arguments here?

  auto projectRoot = std::filesystem::current_path().string();
  IndexerPPConfig config{
      AbsolutePath::tryFrom(std::string_view(projectRoot)).value(),
      HeaderFilter(), false};
  SemanticAnalysisJobResult result{};
  auto frontendActionFactory = IndexerFrontendActionFactory(config, result);

  clang::tooling::ToolInvocation Invocation(
      std::move(args), &frontendActionFactory, fileManager.get(),
      std::make_shared<clang::PCHContainerOperations>());

  IndexerDiagnosticConsumer diagnosticConsumer;
  Invocation.setDiagnosticConsumer(&diagnosticConsumer);

  {
    LogTimerRAII timer(fmt::format("invocation for {}", job.command.Filename));
    bool ranSuccessfully = Invocation.run();
    (void)ranSuccessfully; // FIXME(ref: surface-diagnostics)
  }

  return result;
}

int workerMain(CliOptions &&cliOptions) {
  BOOST_TRY {
    MessageQueuePair mq(cliOptions.driverId, cliOptions.workerId);

    while (true) {
      IndexJobRequest request{};
      auto recvError =
          mq.driverToWorker.timedReceive(request, cliOptions.receiveTimeout);
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
      IndexJobResult result;
      result.kind = request.job.kind;
      switch (request.job.kind) {
      case IndexJob::Kind::EmitIndex:
        result.emitIndex = EmitIndexJobResult{"lol"};
        break;
      case IndexJob::Kind::SemanticAnalysis:
        result.semanticAnalysis = scip_clang::performSemanticAnalysis(
            std::move(request.job.semanticAnalysis));
        break;
      }
      mq.workerToDriver.send(
          IndexJobResponse{cliOptions.workerId, request.id, std::move(result)});
    }
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
