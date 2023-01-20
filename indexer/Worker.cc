#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string_view>
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

#include "scip/scip.pb.h"

#include "indexer/CliOptions.h"
#include "indexer/Comparison.h"
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

struct HistoryEntry {
  llvm::yaml::Hex64 beforeHash;
  llvm::yaml::Hex64 afterHash;
  std::string mixedValue;
  std::string mixContext;
  std::string contextData;
};

namespace {

// A type to keep track of the "transcript" (in Kythe terminology)
// of an #include being processed.
class HashValueBuilder {
public:
  using History = std::vector<HistoryEntry>;

private:
  // The hash value calculated so far for preprocessor effects.
  HashValue runningHash;

  // Optional field to track all the inputs that went into computing
  // a hash, meant for debugging. We buffer all the history for a
  // file instead of directly writing to a stream because if there
  // are multiple files which match, having the output be interleaved
  // (due to the ~DAG nature of includes) would be confusing.
  std::unique_ptr<History> history;

public:
  HashValueBuilder(bool recordHistory)
      : runningHash(),
        history(recordHistory ? std::make_unique<History>() : nullptr) {}

  void mix(std::string_view text) {
    this->runningHash.mix(reinterpret_cast<const uint8_t *>(text.data()),
                          text.size());
  }

  void mix(uint64_t v) {
    this->runningHash.mix(reinterpret_cast<const uint8_t *>(&v), sizeof(v));
  }

  template <typename T> void mixWithContext(T t, HistoryEntry &&entry) {
    ENFORCE(this->isRecordingHistory());
    entry.beforeHash = this->runningHash.rawValue;
    this->mix(t);
    entry.afterHash = this->runningHash.rawValue;
    this->history->emplace_back(std::move(entry));
  }

  std::pair<HashValue, std::unique_ptr<History>> finish() {
    return {this->runningHash, std::move(this->history)};
  }

  bool isRecordingHistory() {
    return (bool)this->history;
  }
};

#define MIX_WITH_KEY(_hash, _value, _path_expr, _context_expr)          \
  {                                                                     \
    if (_hash.isRecordingHistory()) {                                   \
      _hash.mixWithContext(                                             \
          _value, HistoryEntry{.mixedValue = fmt::format("{}", _value), \
                               .mixContext = _context_expr,             \
                               .contextData = _path_expr});             \
    } else {                                                            \
      _hash.mix(_value);                                                \
    }                                                                   \
  }

struct HeaderInfoBuilder final {
  HashValueBuilder hashValueBuilder;
  const clang::FileID fileId;
};

class IndexerPreprocessorStack final {
  std::vector<HeaderInfoBuilder> state;

public:
  bool empty() const {
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

struct IndexerPreprocessorOptions {
  ProjectRootPath projectRootPath;

  // Debugging-related
  PreprocessorHistoryRecorder *recorder;

  // Sort for deterministic output while running the preprocessor.
  bool deterministic;
};

// Small wrapper type for YAML serialization.
struct PreprocessorHistory {
  llvm::StringRef path;
  HashValueBuilder::History &history;
  llvm::yaml::Hex64 finalHashValue;
};

} // namespace
} // namespace scip_clang

template <> struct llvm::yaml::MappingTraits<scip_clang::PreprocessorHistory> {
  static void mapping(llvm::yaml::IO &io,
                      scip_clang::PreprocessorHistory &entry) {
    io.mapRequired("path", entry.path);
    io.mapRequired("hash", entry.finalHashValue);
    io.mapRequired("history", entry.history);
  }
};

template <> struct llvm::yaml::MappingTraits<scip_clang::HistoryEntry> {
  static void mapping(llvm::yaml::IO &io, scip_clang::HistoryEntry &entry) {
    io.mapRequired("before-hash", entry.beforeHash);
    io.mapRequired("mixed-value", entry.mixedValue);
    io.mapOptional("mix-context", entry.mixContext, "");
    io.mapOptional("context-data", entry.contextData, "");
    io.mapRequired("after-hash", entry.afterHash);
  }
};

template <> struct llvm::yaml::SequenceElementTraits<scip_clang::HistoryEntry> {
  static const bool flow = false;
};

namespace scip_clang {
namespace {

struct PreprocessorDebugContext {
  std::string tuMainFilePath;
};

using PathToIdMap = absl::flat_hash_map<AbsolutePathRef, clang::FileID>;

class IndexerPreprocessorWrapper final : public clang::PPCallbacks {
  const IndexerPreprocessorOptions &options;

  clang::SourceManager &sourceManager;
  IndexerPreprocessorStack stack;

  struct MultiHashValue {
    const HashValue hashValue;
    bool isMultiple;
  };
  // Headers which we've seen only expand in a single way.
  // The extra bit inside the MultiHashValue struct indicates
  // if should look in the finishedProcessingMulti map instead.
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>, MultiHashValue>
      finishedProcessing;
  // Headers which expand in at least 2 different ways.
  // The values have size() >= 2.
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>,
                      absl::flat_hash_set<HashValue>>
      finishedProcessingMulti;

  const PreprocessorDebugContext debugContext;

public:
  IndexerPreprocessorWrapper(clang::SourceManager &sourceManager,
                             const IndexerPreprocessorOptions &options,
                             PreprocessorDebugContext &&debugContext)
      : options(options), sourceManager(sourceManager), stack(),
        finishedProcessing(), finishedProcessingMulti(),
        debugContext(std::move(debugContext)) {}

  void flushState(SemanticAnalysisJobResult &result, PathToIdMap &pathToIdMap) {
    bool emittedEmptyPathWarning = false;
    auto getAbsPath =
        [&](clang::FileID fileId) -> std::optional<AbsolutePathRef> {
      ENFORCE(fileId.isValid(), "stored invalid FileID in map!");
      auto entry = this->sourceManager.getFileEntryForID(fileId);
      if (!entry) { // fileId represents an imaginary buffer; ignore those for
                    // indexing
        return {};
      }
      auto path = entry->tryGetRealPathName();
      if (path.empty() && !emittedEmptyPathWarning) {
        spdlog::warn("empty path for FileEntry when indexing {}",
                     this->debugContext.tuMainFilePath);
        emittedEmptyPathWarning = true;
        return {};
      }
      auto optAbsPath = AbsolutePathRef::tryFrom(path);
      if (!optAbsPath.has_value()) {
        spdlog::warn("unexpected relative path from tryGetRealPathName() = {} "
                     "when indexing {}",
                     scip_clang::toStringView(path),
                     this->debugContext.tuMainFilePath);
      }
      return optAbsPath;
    };
    {
      for (auto [wrappedFileId, hashInfo] : this->finishedProcessing) {
        if (hashInfo.isMultiple) {
          continue;
        }
        auto hashValue = hashInfo.hashValue;
        auto fileId = wrappedFileId.data;
        if (auto optPath = getAbsPath(fileId)) {
          auto absPathRef = optPath.value();
          result.wellBehavedFiles.emplace_back(
              PreprocessedFileInfo{AbsolutePath{absPathRef}, hashValue});
          pathToIdMap.insert({absPathRef, fileId});
        }
      }
      if (this->options.deterministic) {
        absl::c_sort(result.wellBehavedFiles);
      }
    }
    {
      auto &fileHashes = this->finishedProcessingMulti;
      for (auto it = fileHashes.begin(), end = fileHashes.end(); it != end;
           ++it) {
        auto fileId = it->first.data;
        if (auto optPath = getAbsPath(fileId)) {
          auto absPathRef = optPath.value();
          std::vector<HashValue> hashes{};
          hashes.reserve(it->second.size());
          absl::c_move(it->second, std::back_inserter(hashes));
          if (this->options.deterministic) {
            absl::c_sort(hashes);
          }
          result.illBehavedFiles.emplace_back(
              PreprocessedFileInfoMulti{AbsolutePath{absPathRef}, std::move(hashes)});
          pathToIdMap.insert({absPathRef, fileId});
        }
      }
      if (this->options.deterministic) {
        absl::c_sort(result.illBehavedFiles);
      }
    }
  }

private:
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
    auto fileInfo = std::move(optHeaderInfo.value());
    bool fileIdMatchesTopOfStack = fileInfo.fileId == fileId;
    if (!fileIdMatchesTopOfStack) {
      ENFORCE(fileIdMatchesTopOfStack,
              "fileId mismatch:\ntop of stack: {}\nexitInclude: {}",
              debug::tryGetPath(this->sourceManager, fileInfo.fileId),
              debug::tryGetPath(this->sourceManager, fileId));
    }

    auto key = LlvmToAbslHashAdapter<clang::FileID>{fileInfo.fileId};
    auto it = this->finishedProcessing.find(key);
    auto [hashValue, history] = fileInfo.hashValueBuilder.finish();
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
      auto path = this->pathKeyForHistory(fileInfo.fileId);
      PreprocessorHistory entry{path, *history.get(), {hashValue.rawValue}};
      this->options.recorder->yamlStream << entry;
    }
    return hashValue;
  }

  std::string pathKeyForHistory(clang::FileID fileId) {
    ENFORCE(this->options.recorder);
    return this->options.recorder
        ->normalizePath(debug::tryGetPath(this->sourceManager, fileId))
        .str();
  }

  // START overrides from PPCallbacks
public:
  /// \param sourceLoc corresponds to the top of the newly entered file (if
  /// valid).
  /// \param reason
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
              clang::SrcMgr::CharacteristicKind /*fileType*/,
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
      MIX_WITH_KEY(this->stack.topHash(), optHash->rawValue,
                   this->pathKeyForHistory(previousFileId),
                   "hash for #include");
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
      if (auto *recorder = this->options.recorder) {
        if (auto *enteredFileEntry =
                this->sourceManager.getFileEntryForID(enteredFileId)) {
          auto path = enteredFileEntry->tryGetRealPathName();
          if (!path.empty() && recorder->filter.matches(path)) {
            this->enterInclude(true, enteredFileId);
            MIX_WITH_KEY(this->stack.topHash(),
                         toStringView(recorder->normalizePath(path)), "",
                         "self path");
            break;
          }
        }
      }
      this->enterInclude(false, enteredFileId);
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

class IndexerAstVisitor;

/// Type to track which files should be indexed.
///
/// For files that do not belong to this project; their symbols should be
/// tracked in external symbols instead of creating a \c scip::Document.
class FilesToBeIndexedMap final {
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>,
                      ProjectRootRelativePathRef>
      map;
  AbsolutePath rootPath;

public:
  FilesToBeIndexedMap() = delete;
  FilesToBeIndexedMap(std::string_view rootPath)
      : map(),
        rootPath(AbsolutePath(AbsolutePathRef::tryFrom(rootPath).value())) {}
  FilesToBeIndexedMap(const FilesToBeIndexedMap &) = delete;
  FilesToBeIndexedMap &operator=(const FilesToBeIndexedMap &) = delete;
  FilesToBeIndexedMap(FilesToBeIndexedMap &&other) = default;
  FilesToBeIndexedMap &operator=(FilesToBeIndexedMap &&) = default;

  /// Returns true iff a new entry was inserted.
  bool insert(clang::FileID fileId, AbsolutePathRef absPath) {
    ENFORCE(fileId.isValid(),
            "invalid FileIDs should be filtered out after preprocessing");
    ENFORCE(!absPath.asStringView().empty(), "inserting file with empty absolute path");

    // FIXME(clarify-root): We should disambiguate between the project root
    // (needed by SCIP) and the build root, which is the directory wrt paths
    // in compile_commands.json can be interpreted (this may not coincide
    // with the directory containing the compile_commands.json!).
    // For example, LLVM's compile_commands.json contains absolute paths,
    // whereas Chromium's uses relative paths; distinguishing between
    // the different roots properly is esp. important in the second case.
    //
    // Right now we are relying on substring checks, but with CMake,
    // it is common to have the build root be inside the project root,
    // so making the distinction is important for CMake too.

    auto rootPathRef = this->rootPath.asRef();
    if (auto relPath = rootPathRef.makeRelative(absPath)) {
      ENFORCE(!relPath->empty(),
              "file path is unexpectedly equal to project root");
      auto [_, inserted] = this->map.insert({{fileId}, ProjectRootRelativePathRef{relPath.value()}});
      return inserted;
    } else {
      auto [_, inserted] = this->map.insert({{fileId}, ProjectRootRelativePathRef{}});
      return inserted;
    }
  }

  void reserve(size_t totalCapacity) {
    this->map.reserve(totalCapacity);
  }

  void forEachProjectLocalFile(
      absl::FunctionRef<void(ProjectRootRelativePathRef)> doStuff) {
    for (auto &[_, relPathRef] : this->map) {
      if (relPathRef.asStringView().empty()) { // external file
        continue;
      }
      doStuff(relPathRef);
    }
  }
};

class IndexerAstVisitor : public clang::RecursiveASTVisitor<IndexerAstVisitor> {
  using Base = RecursiveASTVisitor;

  FilesToBeIndexedMap toBeIndexed;
  bool deterministic;

public:
  IndexerAstVisitor(FilesToBeIndexedMap &&map, bool deterministic)
      : toBeIndexed(std::move(map)), deterministic(deterministic) {}

  void writeIndex(scip::Index &scipIndex) {
    std::vector<ProjectRootRelativePathRef> relativePaths;
    toBeIndexed.forEachProjectLocalFile(
        [&](ProjectRootRelativePathRef relPathRef) -> void {
          relativePaths.push_back(relPathRef);
        });
    if (this->deterministic) {
      absl::c_sort(relativePaths,
                   [](const ProjectRootRelativePathRef &s1,
                      const ProjectRootRelativePathRef &s2) -> bool {
                     auto cmp = cmp::compareStrings(s1.asStringView(), s2.asStringView());
                     ENFORCE(
                         cmp != cmp::Equal,
                         "document with path '{}' is present 2+ times in index",
                         s1.asStringView());
                     return cmp == cmp::Less;
                   });
    }
    for (auto relPathRef : relativePaths) {
      scip::Document document;
      auto relPath = relPathRef.asStringView();
      document.set_relative_path(relPath.data(), relPath.size());
      // FIXME(def: set-language): Use Clang's built-in detection logic here.
      // Q: With Clang's built-in language detection, does the built-in fake
      // header differ between C, C++ and Obj-C (it presumably should?)?
      // Otherwise, do we need to mix in the language into the hash?
      // Or do we fall back to the common denominator (= C)?
      // Or should we add an other_languages in SCIP?
      document.set_language(scip::Language_Name(scip::Language::CPP));
      *scipIndex.add_documents() = std::move(document);
    }
  }

  // For the various hierarchies, see clang/Basic/.*.td files
  // https://sourcegraph.com/search?q=context:global+repo:llvm/llvm-project%24+file:clang/Basic/.*.td&patternType=standard&sm=1&groupBy=repo
};

class IndexerAstConsumer : public clang::SemaConsumer {
  clang::Sema *sema;
  IndexerPreprocessorWrapper *preprocessorWrapper;
  WorkerCallback getEmitIndexDetails;
  scip::Index &scipIndex;
  bool deterministic;

public:
  IndexerAstConsumer(clang::CompilerInstance &, llvm::StringRef /*filepath*/,
                     IndexerPreprocessorWrapper *preprocessorWrapper,
                     WorkerCallback getEmitIndexDetails, scip::Index &scipIndex,
                     bool deterministic)
      : preprocessorWrapper(preprocessorWrapper),
        getEmitIndexDetails(getEmitIndexDetails), scipIndex(scipIndex),
        deterministic(deterministic) {}

  void HandleTranslationUnit(clang::ASTContext &astContext) override {
    // NOTE(ref: preprocessor-traversal-ordering): The call order is
    // 1. The preprocessor wrapper finishes running.
    // 2. This function is called.
    // 3. EndOfMainFile is called in the preprocessor wrapper.
    // 4. The preprocessor wrapper is destroyed.
    //
    // So flush the state from the wrapper in this function, and use
    // it during the traversal (instead of say flushing state in the dtor
    // would arguably be more idiomatic).
    SemanticAnalysisJobResult semaResult{};
    PathToIdMap pathToIdMap{};
    this->preprocessorWrapper->flushState(semaResult, pathToIdMap);

    EmitIndexJobDetails emitIndexDetails{};
    bool shouldEmitIndex =
        this->getEmitIndexDetails(std::move(semaResult), emitIndexDetails);
    if (!shouldEmitIndex) {
      return;
    }

    // See FIXME(ref: clarify-root)
    auto rootPath = std::filesystem::current_path().string();
    auto optRootPathRef = AbsolutePathRef::tryFrom(std::string_view(rootPath));
    ENFORCE(optRootPathRef.has_value());
    (void)optRootPathRef;
    FilesToBeIndexedMap toBeIndexed(rootPath);
    this->computePathsToBeIndexed(astContext, emitIndexDetails, pathToIdMap,
                                  toBeIndexed);

    IndexerAstVisitor visitor{std::move(toBeIndexed), deterministic};
    visitor.VisitTranslationUnitDecl(astContext.getTranslationUnitDecl());
    visitor.writeIndex(scipIndex);
  }

  void InitializeSema(clang::Sema &S) override {
    this->sema = &S;
  }

  void ForgetSema() override {
    this->sema = nullptr;
  }

private:
  void computePathsToBeIndexed(const clang::ASTContext &astContext,
                               const EmitIndexJobDetails &emitIndexDetails,
                               const PathToIdMap &pathToIdMap,
                               FilesToBeIndexedMap &toBeIndexed) {
    toBeIndexed.reserve(1 + emitIndexDetails.filesToBeIndexed.size());

    auto &sourceManager = astContext.getSourceManager();
    auto mainFileId = sourceManager.getMainFileID();
    if (auto *mainFileEntry = sourceManager.getFileEntryForID(mainFileId)) {
      if (auto optMainFileAbsPath =
              AbsolutePathRef::tryFrom(mainFileEntry->tryGetRealPathName())) {
        toBeIndexed.insert(mainFileId, optMainFileAbsPath.value());
      } else {
        spdlog::debug("tryGetRealPathName() returned non-absolute path '{}'",
                      toStringView(mainFileEntry->tryGetRealPathName()));
      }
    }

    for (auto &absPath : emitIndexDetails.filesToBeIndexed) {
      auto absPathRef = absPath.asRef();
      auto it = pathToIdMap.find(absPathRef);
      if (it == pathToIdMap.end()) {
        spdlog::debug(
            "failed to find clang::FileID for path '{}' received from Driver",
            absPathRef.asStringView());
        continue;
      }
      // SAFETY: the key in pathToIdMap (i.e. it->first) will be alive;
      // don't accidentally store a reference into emitIndexJobDetails
      // as emitIndexJobDetails will soon be destroyed
      absPathRef = it->first;
      toBeIndexed.insert(it->second, it->first);
    }
  }
};

class IndexerFrontendAction : public clang::ASTFrontendAction {
  const IndexerPreprocessorOptions &preprocessorOptions;
  WorkerCallback workerCallback;
  scip::Index &scipIndex;

public:
  IndexerFrontendAction(const IndexerPreprocessorOptions &preprocessorOptions,
                        WorkerCallback workerCallback, scip::Index &scipIndex)
      : preprocessorOptions(preprocessorOptions),
        workerCallback(workerCallback), scipIndex(scipIndex) {
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &compilerInstance,
                    llvm::StringRef filepath) override {
    auto &preprocessor = compilerInstance.getPreprocessor();
    auto callbacks = std::make_unique<IndexerPreprocessorWrapper>(
        compilerInstance.getSourceManager(), this->preprocessorOptions,
        PreprocessorDebugContext{filepath.str()});
    // SAFETY: See NOTE(ref: preprocessor-traversal-ordering)
    // Ideally, we'd use a shared_ptr, but addPPCallbacks needs a unique_ptr.
    auto preprocessorWrapper = callbacks.get();
    preprocessor.addPPCallbacks(std::move(callbacks));
    return std::make_unique<IndexerAstConsumer>(
        compilerInstance, filepath, preprocessorWrapper, this->workerCallback,
        this->scipIndex, this->preprocessorOptions.deterministic);
  }
};

class IndexerFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
  const IndexerPreprocessorOptions &preprocessorOptions;
  WorkerCallback workerCallback;
  scip::Index &scipIndex;

public:
  IndexerFrontendActionFactory(const IndexerPreprocessorOptions &preprocessorOptions,
                               WorkerCallback workerCallback,
                               scip::Index &scipIndex)
      : preprocessorOptions(preprocessorOptions),
        workerCallback(workerCallback), scipIndex(scipIndex) {
  }

  virtual std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<IndexerFrontendAction>(
        this->preprocessorOptions, this->workerCallback, this->scipIndex);
  }
};

class IndexerDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level,
                        const clang::Diagnostic &) override {
    // Just dropping all diagnostics on the floor for now.
    // FIXME(def: surface-diagnostics)
  }
};

} // namespace

WorkerOptions WorkerOptions::fromCliOptions(const CliOptions &cliOptions) {
  ProjectRootPath projectRootPath{AbsolutePath{std::filesystem::current_path().string()}};
  return WorkerOptions{projectRootPath,
                       cliOptions.ipcOptions(),
                       cliOptions.logLevel,
                       cliOptions.deterministic,
                       PreprocessorHistoryRecordingOptions{
                           cliOptions.preprocessorRecordHistoryFilterRegex,
                           cliOptions.preprocessorHistoryLogPath, false, ""},
                       cliOptions.temporaryOutputDir,
                       cliOptions.workerFault};
}

Worker::Worker(WorkerOptions &&options)
    : options(std::move(options)),
      messageQueues(
          this->options.ipcOptions.isTestingStub()
              ? nullptr
              : std::make_unique<MessageQueuePair>(
                  MessageQueuePair::forWorker(this->options.ipcOptions))),
      recorder() {
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
                                    scip::Index &scipIndex) {
  clang::FileSystemOptions fileSystemOptions;
  fileSystemOptions.WorkingDir = std::move(job.command.Directory);

  llvm::IntrusiveRefCntPtr<clang::FileManager> fileManager(
      new clang::FileManager(fileSystemOptions, nullptr));

  auto args = std::move(job.command.CommandLine);
  args.push_back("-fsyntax-only");   // Only type-checking, no codegen.
  args.push_back("-Wno-everything"); // Warnings aren't helpful.
  // clang-format off
  // TODO(def: flag-passthrough, issue: https://github.com/sourcegraph/scip-clang/issues/23)
  // Support passing through CLI flags to Clang, similar to --extra-arg in lsif-clang
  // clang-format on

  IndexerPreprocessorOptions preprocessorOptions{
      this->options.projectRootPath,
      this->recorder.has_value() ? &this->recorder->second : nullptr,
      this->options.deterministic};
  auto frontendActionFactory =
      IndexerFrontendActionFactory(preprocessorOptions, workerCallback, scipIndex);

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
}

void Worker::emitIndex(scip::Index &&scipIndex, const StdPath &outputPath) {
  std::ofstream outputStream(outputPath, std::ios_base::out
                                             | std::ios_base::binary
                                             | std::ios_base::trunc);
  if (outputStream.fail()) {
    spdlog::warn("failed to open file to write partial index at '{}' ({})",
                 outputPath.c_str(), std::strerror(errno));
    std::exit(EXIT_FAILURE);
  }
  scipIndex.SerializeToOstream(&outputStream);
}

void Worker::sendResult(JobId requestId, IndexJobResult &&result) {
  this->messageQueues->workerToDriver.send(IndexJobResponse{
      this->ipcOptions().workerId, requestId, std::move(result)});
  this->flushStreams();
}

Worker::ReceiveStatus Worker::processTranslationUnitAndRespond(
    IndexJobRequest &&semanticAnalysisRequest) {
  SemanticAnalysisJobResult semaResult{};
  auto semaRequestId = semanticAnalysisRequest.id;
  Worker::ReceiveStatus innerStatus;
  JobId emitIndexRequestId;
  unsigned callbackInvoked = 0;
  auto callback =
      [this, semaRequestId, &innerStatus, &emitIndexRequestId,
       &callbackInvoked](SemanticAnalysisJobResult &&semaResult,
                         EmitIndexJobDetails &emitIndexDetails) -> bool {
    callbackInvoked++;
    this->sendResult(semaRequestId,
                     IndexJobResult{.kind = IndexJob::Kind::SemanticAnalysis,
                                    .semanticAnalysis = std::move(semaResult)});
    IndexJobRequest emitIndexRequest{};
    innerStatus = this->waitForRequest(emitIndexRequest);
    if (innerStatus != ReceiveStatus::OK) {
      return false;
    }
    ENFORCE(emitIndexRequest.job.kind == IndexJob::Kind::EmitIndex);
    emitIndexDetails = std::move(emitIndexRequest.job.emitIndex);
    emitIndexRequestId = emitIndexRequest.id;
    return true;
  };
  scip::Index scipIndex{};
  auto &semaDetails = semanticAnalysisRequest.job.semanticAnalysis;
  this->processTranslationUnit(std::move(semaDetails), callback, scipIndex);
  ENFORCE(callbackInvoked == 1);
  if (innerStatus != ReceiveStatus::OK) {
    return innerStatus;
  }

  StdPath outputPath =
      (this->options.temporaryOutputDir
       / fmt::format("job-{}-worker-{}.index.scip", emitIndexRequestId.id(),
                     this->ipcOptions().workerId));
  this->emitIndex(std::move(scipIndex), outputPath);
  EmitIndexJobResult emitIndexResult{AbsolutePath{outputPath.string()}};

  this->sendResult(emitIndexRequestId,
                   IndexJobResult{.kind = IndexJob::Kind::EmitIndex,
                                  .emitIndex = std::move(emitIndexResult)});
  return Worker::ReceiveStatus::OK;
}

void Worker::flushStreams() {
  if (this->recorder) {
    this->recorder->first->flush();
  }
}

void Worker::triggerFaultIfApplicable() const {
  auto &fault = this->options.workerFault;
  if (fault.empty()) {
    return;
  }
  if (fault == "crash") {
    const char *p = nullptr;
    asm volatile("" ::: "memory");
    spdlog::warn("about to crash");
    char x = *p;
    (void)x;
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
  auto recvError = this->messageQueues->driverToWorker.timedReceive(
      request, this->ipcOptions().receiveTimeout);
  using Status = Worker::ReceiveStatus;
  if (recvError.isA<TimeoutError>()) {
    spdlog::error("timeout in worker; is the driver dead?... shutting down");
    return Status::DriverTimeout;
  }
  if (recvError) {
    spdlog::error("received malformed message: {}",
                  scip_clang::formatLlvm(recvError));
    return Status::MalformedMessage;
  }
  if (request.id == JobId::Shutdown()) {
    spdlog::debug("shutting down");
    return Status::Shutdown;
  }
  spdlog::debug("received job {}", request.id.id());
  this->triggerFaultIfApplicable();
  return Status::OK;
}

void Worker::run() {
  ENFORCE(this->messageQueues,
          "Called Worker::run() while initializing worker in testing");
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
