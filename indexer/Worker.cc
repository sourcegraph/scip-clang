#include <chrono>
#include <compare>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/YAMLTraits.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/CliOptions.h"
#include "indexer/Comparison.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/DebugHelpers.h"
#include "indexer/Derive.h"
#include "indexer/Enforce.h"
#include "indexer/Exception.h"
#include "indexer/Hash.h"
#include "indexer/Indexer.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Logging.h"
#include "indexer/Path.h"
#include "indexer/ScipExtras.h"
#include "indexer/Statistics.h"
#include "indexer/SymbolFormatter.h"
#include "indexer/Timer.h"
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

#define MIX_INTO_HASH(_hash, _value, _path_expr, _context_expr)         \
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
  size_t size() const {
    return this->state.size();
  }
  std::string debugToString(const clang::SourceManager &sourceManager) const {
    std::string buf = fmt::format("[{}]{{", this->state.size());
    llvm::raw_string_ostream os(buf);
    for (size_t i = 0; i < this->state.size(); ++i) {
      size_t j = this->state.size() - i - 1;
      os << debug::tryGetPath(sourceManager, this->state[j].fileId);
      if (j != 0) {
        os << ", ";
      }
    }
    os << "}";
    return buf;
  }
};

struct IndexerPreprocessorOptions {
  RootPath projectRootPath;

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

/// Type to retrieve information about the \c clang::FileID corresponding
/// to a (HashValue, Path) pair.
///
/// The worker and driver communicate using (HashValue, Path) pairs,
/// since those are stable across different workers running in parallel.
///
/// However, inside a worker, we'd like to use \c clang::FileID keys if
/// possible (e.g. storing Documents before indexing), since they are
/// 32-bit integer values. This map translates driver->worker info
/// in (HashValue, Path) terms to FileIDs.
///
/// In general, it may be the case that multiple FileIDs correspond to
/// the same (HashValue, Path) pair (this happens for well-behaved
/// headers, c.f. \c FileIndexingPlanner); the representative FileID
/// is chosen arbitrarily.
class ClangIdLookupMap {
  struct Value {
    absl::flat_hash_map<HashValue, clang::FileID> hashToFileId;
  };

  absl::flat_hash_map<AbsolutePathRef, std::shared_ptr<Value>> impl;

public:
  ClangIdLookupMap() = default;

  void insert(AbsolutePathRef absPathRef, HashValue hashValue,
              clang::FileID fileId) {
    auto it = this->impl.find(absPathRef);
    if (it == this->impl.end()) {
      this->impl.emplace(absPathRef,
                         std::make_shared<Value>(
                             Value{.hashToFileId = {{hashValue, fileId}}}));
      return;
    }
    // A single representative FileID is sufficient.
    it->second->hashToFileId[hashValue] = fileId;
  }

  void forEachPathAndHash(
      absl::FunctionRef<void(AbsolutePathRef, const absl::flat_hash_map<
                                                  HashValue, clang::FileID> &)>
          callback) const {
    for (auto &[absPathRef, valuePtr] : this->impl) {
      ENFORCE(!valuePtr->hashToFileId.empty(),
              "Shouldn't have stored empty maps");
      callback(absPathRef, valuePtr->hashToFileId);
    }
  }

  std::optional<clang::FileID> lookup(AbsolutePathRef absPathRef,
                                      HashValue hashValue) const {
    auto it = this->impl.find(absPathRef);
    if (it == this->impl.end()) {
      return {};
    }
    auto hashIt = it->second->hashToFileId.find(hashValue);
    if (hashIt == it->second->hashToFileId.end()) {
      return {};
    }
    return hashIt->second;
  }

  std::optional<clang::FileID>
  lookupAnyFileId(AbsolutePathRef absPathRef) const {
    auto it = this->impl.find(absPathRef);
    if (it == this->impl.end()) {
      return {};
    }
    for (auto [hashValue, fileId] : it->second->hashToFileId) {
      return fileId;
    }
    ENFORCE(false, "Shouldn't have stored empty maps");
    return {};
  }
};

class IndexerPreprocessorWrapper final : public clang::PPCallbacks {
  const IndexerPreprocessorOptions &options;

  clang::SourceManager &sourceManager;
  IndexerPreprocessorStack stack;

  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>, HashValue>
      finishedProcessing;

  MacroIndexer macroIndexer;

  const PreprocessorDebugContext debugContext;

public:
  IndexerPreprocessorWrapper(clang::SourceManager &sourceManager,
                             const IndexerPreprocessorOptions &options,
                             PreprocessorDebugContext &&debugContext)
      : options(options), sourceManager(sourceManager), stack(),
        finishedProcessing(), macroIndexer(sourceManager),
        debugContext(std::move(debugContext)) {}

  void flushState(SemanticAnalysisJobResult &result,
                  ClangIdLookupMap &clangIdLookupMap,
                  MacroIndexer &macroIndexerOutput) {
    // HACK: It seems like EnterInclude and ExitInclude events are not
    // perfectly balanced in Clang. Work around that.
    auto mainFileId = this->sourceManager.getMainFileID();
    // When working with already pre-processed files (mainly invoked by
    // C-Reduce), we can end up having hundreds of superfluous residual entries
    // in the stack.
    if (this->stack.size() > 2) {
      for (size_t i = 0, toDrain = this->stack.size() - 2; i < toDrain; ++i) {
        auto redundantEntry = this->stack.pop();
        ENFORCE(redundantEntry->fileId == mainFileId);
      }
    }
    auto lastEntry = this->stack.pop();
    ENFORCE(lastEntry.has_value());
    ENFORCE(this->sourceManager.getFileEntryForID(lastEntry->fileId) == nullptr,
            "carelessly popped entry for '{}' without exiting",
            debug::tryGetPath(this->sourceManager, lastEntry->fileId));
    this->exitFile(mainFileId);
    ENFORCE(this->stack.empty(), "entry for '{}' present at top of stack",
            debug::tryGetPath(this->sourceManager, this->stack.pop()->fileId));
    // END HACK

    macroIndexerOutput = std::move(this->macroIndexer);

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
                     llvm_ext::toStringView(path),
                     this->debugContext.tuMainFilePath);
      }
      return optAbsPath;
    };
    for (auto [wrappedFileId, hashValue] : this->finishedProcessing) {
      auto fileId = wrappedFileId.data;
      if (auto optPath = getAbsPath(fileId)) {
        auto absPathRef = optPath.value();
        clangIdLookupMap.insert(absPathRef, hashValue, fileId);
      }
    }
    clangIdLookupMap.forEachPathAndHash(
        [&](AbsolutePathRef absPathRef,
            const absl::flat_hash_map<HashValue, clang::FileID> &map) {
          if (map.size() == 1) {
            for (auto &[hashValue, fileId] : map) {
              result.wellBehavedFiles.emplace_back(
                  PreprocessedFileInfo{AbsolutePath{absPathRef}, hashValue});
            }
          } else {
            std::vector<HashValue> hashes;
            hashes.reserve(map.size());
            for (auto &[hashValue, fileId] : map) {
              hashes.push_back(hashValue);
            }
            result.illBehavedFiles.emplace_back(PreprocessedFileInfoMulti{
                AbsolutePath{absPathRef}, std::move(hashes)});
          }
        });
    if (this->options.deterministic) {
      absl::c_sort(result.wellBehavedFiles);
      absl::c_sort(result.illBehavedFiles);
    }
  }

private:
  void enterFile(clang::FileID enteredFileId) {
    // NOTE(def: skip-invalid-fileids)
    // Not 100% sure what are all the situations when these can arise,
    // but they do not correspond to actual files (with paths), so skip them
    if (!enteredFileId.isValid()) {
      return;
    }
    if (auto *recorder = this->options.recorder) {
      if (auto *enteredFileEntry =
              this->sourceManager.getFileEntryForID(enteredFileId)) {
        auto path = enteredFileEntry->tryGetRealPathName();
        if (!path.empty() && recorder->filter.matches(path)) {
          this->enterFileImpl(true, enteredFileId);
          MIX_INTO_HASH(this->stack.topHash(),
                        llvm_ext::toStringView(recorder->normalizePath(path)),
                        "", "self path");
          return;
        }
      }
    }
    this->enterFileImpl(false, enteredFileId);
  }

  void enterFileImpl(bool recordHistory, clang::FileID enteredFileId) {
    this->stack.push(
        HeaderInfoBuilder{HashValueBuilder(recordHistory), enteredFileId});
  }

  void exitFile(clang::FileID previousFileId) {
    auto optHash = this->exitFileImpl(previousFileId);
    if (!optHash || this->stack.empty()) {
      return;
    }
    MIX_INTO_HASH(this->stack.topHash(), optHash->rawValue,
                  this->pathKeyForHistory(previousFileId), "hash for #include");
  }

  std::optional<HashValue> exitFileImpl(clang::FileID fileId) {
    if (fileId.isInvalid()) { // See NOTE(ref: skip-invalid-fileids)
      return {}; // Didn't get pushed onto the stack, so nothing to return
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

    auto key = llvm_ext::AbslHashAdapter<clang::FileID>{fileInfo.fileId};
    auto [hashValue, history] = fileInfo.hashValueBuilder.finish();
    auto it = this->finishedProcessing.find(key);
    if (it == this->finishedProcessing.end()) {
      this->finishedProcessing.insert({key, hashValue});
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
      this->exitFile(previousFileId);
      break;
    }
    case Reason::EnterFile: {
      if (!sourceLoc.isValid()) {
        break;
      }
      ENFORCE(sourceLoc.isFileID(), "EnterFile called on a non-FileID");
      auto enteredFileId = this->sourceManager.getFileID(sourceLoc);
      this->enterFile(enteredFileId);
      break;
    }
    }
  }

  /// Hook called whenever a macro definition is seen.
  virtual void
  MacroDefined(const clang::Token &macroNameToken,
               const clang::MacroDirective *macroDirective) override {
    ENFORCE(macroDirective != nullptr);
    auto *macroInfo = macroDirective->getMacroInfo();
    this->macroIndexer.saveDefinition(macroNameToken, macroInfo);
    // FIXME: Mix the macro definition into the running hash
  }

  // TODO: Document the differences between the types
  // MacroDefinition
  // MacroInfo
  // MacroDirective

  virtual void MacroUndefined(const clang::Token &macroNameToken,
                              const clang::MacroDefinition &macroDefinition,
                              const clang::MacroDirective *) override {
    // FIXME: Mix the undef into the running hash
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
  }

  /// \param range is a closed interval covering the full argument list
  ///    For example, in an expansion ABC(10), \c range.start is the location
  ///    of A and \c range.end is the location of the ).
  virtual void MacroExpands(const clang::Token &macroNameToken,
                            const clang::MacroDefinition &macroDefinition,
                            clang::SourceRange,
                            const clang::MacroArgs *) override {
    // TODO: Handle macro arguments
    // Q: How/when should we use the SourceRange argument
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
    // FIXME: Mix the expands into the running hash
  }

  virtual void Ifdef(clang::SourceLocation, const clang::Token &macroNameToken,
                     const clang::MacroDefinition &macroDefinition) override {
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
    // FIXME: Mix the ifdef into the running hash.
  }

  virtual void Ifndef(clang::SourceLocation, const clang::Token &macroNameToken,
                      const clang::MacroDefinition &macroDefinition) override {
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
  }

  virtual void Defined(const clang::Token &macroNameToken,
                       const clang::MacroDefinition &macroDefinition,
                       clang::SourceRange) override {
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
  }

  virtual void Elifdef(clang::SourceLocation,
                       const clang::Token &macroNameToken,
                       const clang::MacroDefinition &macroDefinition) override {
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
  }

  virtual void
  Elifndef(clang::SourceLocation, const clang::Token &macroNameToken,
           const clang::MacroDefinition &macroDefinition) override {
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
  }

  virtual void InclusionDirective(
      clang::SourceLocation /*hashLoc*/, const clang::Token & /*includeTok*/,
      llvm::StringRef /*fileName*/, bool /*isAngled*/,
      clang::CharSourceRange fileNameRange,
      clang::OptionalFileEntryRef optFileEntry, clang::StringRef /*searchPath*/,
      clang::StringRef /*relativePath*/, const clang::Module * /*importModule*/,
      clang::SrcMgr::CharacteristicKind /*fileType*/) override {
    if (!optFileEntry.has_value() || fileNameRange.isInvalid()) {
      return;
    }
    auto fileId = this->sourceManager.getFileID(fileNameRange.getBegin());
    if (fileId.isInvalid()) {
      return;
    }
    auto realPath = optFileEntry->getFileEntry().tryGetRealPathName();
    if (auto optAbsPathRef = AbsolutePathRef::tryFrom(realPath)) {
      this->macroIndexer.saveInclude(fileId, fileNameRange.getAsRange(),
                                     *optAbsPathRef);
    }
  }

  // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/21):
  // Add overrides for
  // - If
  // - Elif

  // END overrides from PPCallbacks
};

class IndexerAstVisitor;

/// Type to track which files should be indexed.
///
/// For files that do not belong to this project; their symbols should be
/// tracked in external symbols instead of creating a \c scip::Document.
///
/// Not every file that is part of this project will be part of this map.
/// For example, if a file+hash was already indexed by another worker,
/// then one shouldn't call insert(..) for that file.
using FileIdsToBeIndexedSet =
    absl::flat_hash_set<llvm_ext::AbslHashAdapter<clang::FileID>>;

/// Type to track canonical relative paths for FileIDs.
///
/// Two types of files may not have canonical relative paths:
/// 1. Paths inside the build root without a corresponding file inside
///    the project root.
/// 2. Files from outside the project.
///
/// In the future, for cross-repo, a directory layout<->project mapping
/// may be supplied or inferred, which would provide canonical relative
/// paths for more files.
class StableFileIdMap final {
  using Self = StableFileIdMap;

  std::vector<RootRelativePath> storage;

  struct ExternalFileEntry {
    AbsolutePathRef absPath;
    // Points to storage
    RootRelativePathRef fakeRelativePath;
  };

  using MapValueType = std::variant<RootRelativePathRef, ExternalFileEntry>;

  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>, MapValueType>
      map;

  const RootPath &projectRootPath;

  const RootPath &buildRootPath;

public:
  StableFileIdMap() = delete;
  StableFileIdMap(const RootPath &projectRootPath,
                  const RootPath &buildRootPath)
      : map(), projectRootPath(projectRootPath), buildRootPath(buildRootPath) {}
  StableFileIdMap(StableFileIdMap &&other) = default;
  StableFileIdMap &operator=(StableFileIdMap &&) = delete;
  StableFileIdMap(const StableFileIdMap &) = delete;
  StableFileIdMap &operator=(const StableFileIdMap &) = delete;

  void populate(const ClangIdLookupMap &clangIdLookupMap) {
    clangIdLookupMap.forEachPathAndHash( // force formatting break
        [&](AbsolutePathRef absPathRef,
            const absl::flat_hash_map<HashValue, clang::FileID> &map) {
          for (auto &[hash, fileId] : map) {
            bool inserted = this->insert(fileId, absPathRef);
            ENFORCE(inserted,
                    "there is a 1-1 mapping from FileID -> (path, hash) so the"
                    " FileID {} for {} should not have been inserted earlier",
                    fileId.getHashValue(), absPathRef.asStringView());
          }
        });
  }

  /// Returns true iff a new entry was inserted.
  bool insert(clang::FileID fileId, AbsolutePathRef absPathRef) {
    ENFORCE(fileId.isValid(),
            "invalid FileIDs should be filtered out after preprocessing");
    ENFORCE(!absPathRef.asStringView().empty(),
            "inserting file with empty absolute path");

    auto insertRelPath = [&](RootRelativePathRef projectRootRelPath) -> bool {
      ENFORCE(!projectRootRelPath.asStringView().empty(),
              "file path is unexpectedly equal to project root");
      return this->map.insert({{fileId}, projectRootRelPath}).second;
    };

    // In practice, CMake ends up passing paths to project files as well
    // as files inside the build root. Normally, files inside the build root
    // are generated ones, but to be safe, check if the corresponding file
    // exists in the project. Since the build root itself is typically inside
    // the project root, check the build root first.
    if (auto buildRootRelPath =
            this->buildRootPath.tryMakeRelative(absPathRef)) {
      auto originalFileSourcePath =
          this->projectRootPath.makeAbsoluteAllowKindMismatch(
              buildRootRelPath.value());
      std::error_code error{};
      if (std::filesystem::exists(originalFileSourcePath.asStringRef(), error)
          && !error) {
        return insertRelPath(RootRelativePathRef(
            buildRootRelPath->asStringView(), RootKind::Project));
      }
    } else if (auto optProjectRootRelPath =
                   this->projectRootPath.tryMakeRelative(absPathRef)) {
      return insertRelPath(optProjectRootRelPath.value());
    }
    auto optFileName = absPathRef.fileName();
    ENFORCE(optFileName.has_value(),
            "Clang returned file path {} without a file name",
            absPathRef.asStringView());
    this->storage.emplace_back(
        fmt::format("<external>/{}/{}",
                    HashValue::forText(absPathRef.asStringView()),
                    *optFileName),
        RootKind::Build); // fake value to satisfy the RootRelativePathRef API
    return this->map
        .insert({{fileId},
                 ExternalFileEntry{absPathRef, this->storage.back().asRef()}})
        .second;
  }

  bool contains(clang::FileID fileId) const {
    return this->map.contains({fileId});
  }

  /// See the doc comment on \c StableFileIdMap
  std::optional<StableFileId> getStableFileId(clang::FileID fileId) const {
    auto it = this->map.find({fileId});
    if (it == this->map.end()) {
      return {};
    }
    return Self::mapValueToStableFileId(it->second);
  }

  void
  forEachFileId(absl::FunctionRef<void(clang::FileID, StableFileId)> callback) {
    for (auto &[wrappedFileId, entry] : this->map) {
      callback(wrappedFileId.data, Self::mapValueToStableFileId(entry));
    }
  }

private:
  static StableFileId mapValueToStableFileId(const MapValueType &variant) {
    if (std::holds_alternative<RootRelativePathRef>(variant)) {
      return StableFileId{.path = std::get<RootRelativePathRef>(variant),
                          .isInProject = true,
                          .isSynthetic = false};
    }
    return StableFileId{
        .path = std::get<ExternalFileEntry>(variant).fakeRelativePath,
        .isInProject = false,
        .isSynthetic = true};
  }
};

class IndexerAstVisitor : public clang::RecursiveASTVisitor<IndexerAstVisitor> {
  using Base = RecursiveASTVisitor;

  const StableFileIdMap &stableFileIdMap;
  FileIdsToBeIndexedSet toBeIndexed;
  bool deterministic;

  TuIndexer &tuIndexer;

public:
  IndexerAstVisitor(const StableFileIdMap &pathMap,
                    FileIdsToBeIndexedSet &&toBeIndexed, bool deterministic,
                    TuIndexer &tuIndexer)
      : stableFileIdMap(pathMap), toBeIndexed(std::move(toBeIndexed)),
        deterministic(deterministic), tuIndexer(tuIndexer) {}

  // See clang/include/clang/Basic/DeclNodes.td for list of declarations.

#define VISIT_DECL(DeclName)                                         \
  bool Visit##DeclName##Decl(clang::DeclName##Decl *decl) {          \
    ENFORCE(decl, "expected visitor to only access non-null decls"); \
    this->tuIndexer.save##DeclName##Decl(*decl);                     \
    return true;                                                     \
  }
  FOR_EACH_DECL_TO_BE_INDEXED(VISIT_DECL)
#undef VISIT_DECL

#define VISIT_EXPR(ExprName)                                     \
  bool Visit##ExprName##Expr(clang::ExprName##Expr *expr) {      \
    ENFORCE(expr, "expected " #ExprName "Expr to be null-null"); \
    this->tuIndexer.save##ExprName##Expr(*expr);                 \
    return true;                                                 \
  }
  FOR_EACH_EXPR_TO_BE_INDEXED(VISIT_EXPR)
#undef VISIT_EXPR

#define VISIT_TYPE_LOC(TypeName)                                    \
  bool Visit##TypeName##TypeLoc(clang::TypeName##TypeLoc typeLoc) { \
    this->tuIndexer.save##TypeName##TypeLoc(typeLoc);               \
    return true;                                                    \
  }
  FOR_EACH_TYPE_TO_BE_INDEXED(VISIT_TYPE_LOC)
#undef VISIT_TYPE_LOC

  /// Unlike many other entities, there is no corresponding Visit* method in
  /// RecursiveTypeVisitor, so override the Traverse* method instead.
  bool TraverseNestedNameSpecifierLoc(
      const clang::NestedNameSpecifierLoc nestedNameSpecifierLoc) {
    if (nestedNameSpecifierLoc) {
      this->tuIndexer.saveNestedNameSpecifierLoc(nestedNameSpecifierLoc);
    }
    return true;
  }

#define TRY_TO(CALL_EXPR)              \
  do {                                 \
    if (!this->getDerived().CALL_EXPR) \
      return false;                    \
  } while (false)

  /// Replace the default implementation of the Traverse* method as there
  /// is no matching Visit* method, and the default implementation
  /// does not visit member field references.
  /// See https://github.com/llvm/llvm-project/issues/61602
  bool TraverseConstructorInitializer(
      const clang::CXXCtorInitializer *cxxCtorInitializer) {
    if (clang::TypeSourceInfo *TInfo =
            cxxCtorInitializer->getTypeSourceInfo()) {
      TRY_TO(TraverseTypeLoc(TInfo->getTypeLoc()));
    }
    if (clang::FieldDecl *fieldDecl = cxxCtorInitializer->getAnyMember()) {
      this->tuIndexer.saveFieldReference(
          *fieldDecl, cxxCtorInitializer->getSourceLocation());
    }
    if (cxxCtorInitializer->isWritten()
        || this->getDerived().shouldVisitImplicitCode()) {
      TRY_TO(TraverseStmt(cxxCtorInitializer->getInit()));
    }
    return true;
  }
#undef TRY_TO

  void writeIndex(SymbolFormatter &&symbolFormatter, MacroIndexer &&macroIndex,
                  TuIndexingOutput &tuIndexingOutput) {
    std::vector<std::pair<RootRelativePathRef, clang::FileID>>
        indexedProjectFiles;
    for (auto wrappedFileId : this->toBeIndexed) {
      if (auto optStableFileId =
              this->stableFileIdMap.getStableFileId(wrappedFileId.data)) {
        if (optStableFileId->isInProject) {
          indexedProjectFiles.emplace_back(optStableFileId->path,
                                           wrappedFileId.data);
        }
      }
    }
    if (this->deterministic) {
      auto comparePaths = [](const auto &p1, const auto &p2) -> bool {
        auto cmp = p1.first <=> p2.first;
        ENFORCE(cmp != 0,
                "document with path '{}' is present 2+ times in index",
                p1.first.asStringView());
        return cmp == std::strong_ordering::less;
      };
      absl::c_sort(indexedProjectFiles, comparePaths);
    }

    for (auto [relPathRef, fileId] : indexedProjectFiles) {
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
      macroIndex.emitDocumentOccurrencesAndSymbols(
          this->deterministic, symbolFormatter, fileId, document);
      this->tuIndexer.emitDocumentOccurrencesAndSymbols(this->deterministic,
                                                        fileId, document);
      *tuIndexingOutput.docsAndExternals.add_documents() = std::move(document);
    }
    this->tuIndexer.emitExternalSymbols(deterministic,
                                        tuIndexingOutput.docsAndExternals);
    this->tuIndexer.emitForwardDeclarations(deterministic,
                                            tuIndexingOutput.forwardDecls);
    macroIndex.emitExternalSymbols(this->deterministic, symbolFormatter,
                                   tuIndexingOutput.docsAndExternals);
  }

  // For the various hierarchies, see clang/Basic/.*.td files
  // https://sourcegraph.com/search?q=context:global+repo:llvm/llvm-project%24+file:clang/Basic/.*.td&patternType=standard&sm=1&groupBy=repo
};

struct IndexerAstConsumerOptions {
  RootPath projectRootPath;
  RootPath buildRootPath;
  WorkerCallback getEmitIndexDetails;
  bool deterministic;
};

class IndexerAstConsumer : public clang::SemaConsumer {
  const IndexerAstConsumerOptions &options;
  IndexerPreprocessorWrapper *preprocessorWrapper;
  clang::Sema *sema;
  TuIndexingOutput &tuIndexingOutput;

public:
  IndexerAstConsumer(clang::CompilerInstance &, llvm::StringRef /*filepath*/,
                     const IndexerAstConsumerOptions &options,
                     IndexerPreprocessorWrapper *preprocessorWrapper,
                     TuIndexingOutput &tuIndexingOutput)
      : options(options), preprocessorWrapper(preprocessorWrapper),
        sema(nullptr), tuIndexingOutput(tuIndexingOutput) {}

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
    ClangIdLookupMap clangIdLookupMap{};
    auto &sourceManager = astContext.getSourceManager();
    MacroIndexer macroIndexer{sourceManager};
    this->preprocessorWrapper->flushState(semaResult, clangIdLookupMap,
                                          macroIndexer);

    EmitIndexJobDetails emitIndexDetails{};
    bool shouldEmitIndex = this->options.getEmitIndexDetails(
        std::move(semaResult), emitIndexDetails);
    if (!shouldEmitIndex) {
      return;
    }

    StableFileIdMap stableFileIdMap{this->options.projectRootPath,
                                    this->options.buildRootPath};
    FileIdsToBeIndexedSet toBeIndexed{};
    this->computeFileIdsToBeIndexed(astContext, emitIndexDetails,
                                    clangIdLookupMap, stableFileIdMap,
                                    toBeIndexed);

    auto getStableFileId =
        [&](clang::FileID fileId) -> std::optional<StableFileId> {
      return stableFileIdMap.getStableFileId(fileId);
    };
    SymbolFormatter symbolFormatter{sourceManager, getStableFileId};
    TuIndexer tuIndexer{sourceManager, this->sema->getLangOpts(),
                        this->sema->getASTContext(), symbolFormatter,
                        getStableFileId};

    this->saveIncludeReferences(toBeIndexed, macroIndexer, clangIdLookupMap,
                                stableFileIdMap, tuIndexer);

    IndexerAstVisitor visitor{stableFileIdMap, std::move(toBeIndexed),
                              this->options.deterministic, tuIndexer};
    visitor.TraverseAST(astContext);

    visitor.writeIndex(std::move(symbolFormatter), std::move(macroIndexer),
                       this->tuIndexingOutput);
  }

  void InitializeSema(clang::Sema &S) override {
    this->sema = &S;
  }

  void ForgetSema() override {
    this->sema = nullptr;
  }

private:
  void computeFileIdsToBeIndexed(const clang::ASTContext &astContext,
                                 const EmitIndexJobDetails &emitIndexDetails,
                                 const ClangIdLookupMap &clangIdLookupMap,
                                 StableFileIdMap &stableFileIdMap,
                                 FileIdsToBeIndexedSet &toBeIndexed) {
    auto &sourceManager = astContext.getSourceManager();
    auto mainFileId = sourceManager.getMainFileID();

    stableFileIdMap.populate(clangIdLookupMap);
    if (auto *mainFileEntry = sourceManager.getFileEntryForID(mainFileId)) {
      if (auto optMainFileAbsPath =
              AbsolutePathRef::tryFrom(mainFileEntry->tryGetRealPathName())) {
        stableFileIdMap.insert(mainFileId, optMainFileAbsPath.value());
        toBeIndexed.insert({mainFileId});
      } else {
        spdlog::debug(
            "tryGetRealPathName() returned non-absolute path '{}'",
            llvm_ext::toStringView(mainFileEntry->tryGetRealPathName()));
      }
    }

    for (auto &fileInfo : emitIndexDetails.filesToBeIndexed) {
      auto absPathRef = fileInfo.path.asRef();
      auto optFileId = clangIdLookupMap.lookup(absPathRef, fileInfo.hashValue);
      if (!optFileId.has_value()) {
        spdlog::debug(
            "failed to find clang::FileID for path '{}' received from Driver",
            absPathRef.asStringView());
        continue;
      }
      toBeIndexed.insert({*optFileId});
    }
  }

  void saveIncludeReferences(const FileIdsToBeIndexedSet &toBeIndexed,
                             const MacroIndexer &macroIndexer,
                             const ClangIdLookupMap &clangIdLookupMap,
                             const StableFileIdMap &stableFileIdMap,
                             TuIndexer &tuIndexer) {
    for (auto &wrappedFileId : toBeIndexed) {
      if (auto optStableFileId =
              stableFileIdMap.getStableFileId(wrappedFileId.data)) {
        tuIndexer.saveSyntheticFileDefinition(wrappedFileId.data,
                                              *optStableFileId);
      }
      macroIndexer.forEachIncludeInFile(
          wrappedFileId.data,
          [&](clang::SourceRange range, AbsolutePathRef importedFilePath) {
            auto optRefFileId =
                clangIdLookupMap.lookupAnyFileId(importedFilePath);
            if (!optRefFileId.has_value()) {
              return;
            }
            auto refFileId = *optRefFileId;
            auto optStableFileId =
                stableFileIdMap.getStableFileId(*optRefFileId);
            ENFORCE(optStableFileId.has_value(),
                    "missing StableFileId value for path {} (FileID = {})",
                    importedFilePath.asStringView(), refFileId.getHashValue());
            tuIndexer.saveInclude(range, *optStableFileId);
          });
    }
  }
};

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
                       cliOptions.showCompilerDiagonstics,
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
    : options(std::move(options)), messageQueues(), compileCommands(),
      commandIndex(0), recorder(), statistics() {
  switch (this->options.mode) {
  case WorkerMode::Ipc:
    this->messageQueues = std::make_unique<MessageQueuePair>(
        MessageQueuePair::forWorker(this->options.ipcOptions));
    break;
  case WorkerMode::Compdb: {
    auto compdbFile = compdb::CompilationDatabaseFile::openAndExitOnErrors(
        this->options.compdbPath,
        compdb::ValidationOptions{.checkDirectoryPathsAreAbsolute = true});
    compdb::ResumableParser parser{};
    // See FIXME(ref: resource-dir-extra)
    parser.initialize(compdbFile, std::numeric_limits<size_t>::max(), true);
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
      AbsolutePathRef::tryFrom(std::string_view(job.command.Directory));
  ENFORCE(optPathRef.has_value()); // See NOTE(ref: directory-field-is-absolute)
  RootPath buildRootPath{AbsolutePath{optPathRef.value()}, RootKind::Build};

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
  IndexerAstConsumerOptions astConsumerOptions{
      this->options.projectRootPath, buildRootPath, std::move(workerCallback),
      this->options.deterministic};
  auto frontendActionFactory = IndexerFrontendActionFactory(
      preprocessorOptions, astConsumerOptions, tuIndexingOutput);

  clang::tooling::ToolInvocation invocation(
      std::move(args), &frontendActionFactory, fileManager.get(),
      std::make_shared<clang::PCHContainerOperations>());

  SuppressDiagnosticConsumer suppressDiagnostics;
  if (!this->options.showCompilerDiagonstics) {
    invocation.setDiagnosticConsumer(&suppressDiagnostics);
  }

  {
    LogTimerRAII timer(fmt::format("invocation for {}", job.command.Filename));
    bool ranSuccessfully = invocation.run();
    (void)ranSuccessfully;
  }
}

void Worker::emitIndex(scip::Index &&scipIndex, const StdPath &outputPath) {
  std::ofstream outputStream(outputPath, std::ios_base::out
                                             | std::ios_base::binary
                                             | std::ios_base::trunc);
  if (outputStream.fail()) {
    spdlog::warn("failed to open file to write shard at '{}' ({})",
                 outputPath.c_str(), std::strerror(errno));
    std::exit(EXIT_FAILURE);
  }
  scipIndex.SerializeToOstream(&outputStream);
}

void Worker::sendResult(JobId requestId, IndexJobResult &&result) {
  ENFORCE(this->options.mode == WorkerMode::Ipc);
  this->messageQueues->workerToDriver.send(IndexJobResponse{
      this->ipcOptions().workerId, requestId, std::move(result)});
  this->flushStreams();
}

Worker::ReceiveStatus Worker::processTranslationUnitAndRespond(
    IndexJobRequest &&semanticAnalysisRequest) {
  ManualTimer indexingTimer{};
  indexingTimer.start();

  SemanticAnalysisJobResult semaResult{};
  auto semaRequestId = semanticAnalysisRequest.id;
  auto tuMainFilePath =
      semanticAnalysisRequest.job.semanticAnalysis.command.Filename;
  Worker::ReceiveStatus innerStatus;
  JobId emitIndexRequestId;
  unsigned callbackInvoked = 0;
  auto callback =
      [this, semaRequestId, &innerStatus, &emitIndexRequestId,
       &callbackInvoked](SemanticAnalysisJobResult &&semaResult,
                         EmitIndexJobDetails &emitIndexDetails) -> bool {
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
  TuIndexingOutput tuIndexingOutput{};
  auto &semaDetails = semanticAnalysisRequest.job.semanticAnalysis;

  scip_clang::exceptionContext =
      fmt::format("processing {}", semaDetails.command.Filename);
  this->processTranslationUnit(std::move(semaDetails), callback,
                               tuIndexingOutput);
  scip_clang::exceptionContext = "";

  ENFORCE(callbackInvoked == 1,
          "callbackInvoked = {} for TU with main file '{}'", callbackInvoked,
          tuMainFilePath);
  if (innerStatus != ReceiveStatus::OK) {
    return innerStatus;
  }

  auto stopTimer = [&]() -> void {
    indexingTimer.stop();
    this->statistics.totalTimeMicros =
        uint64_t(indexingTimer.value<std::chrono::microseconds>());
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

  StdPath prefix =
      this->options.temporaryOutputDir
      / fmt::format("job-{}-worker-{}", emitIndexRequestId.taskId(),
                    this->ipcOptions().workerId);
  StdPath docsAndExternalsOutputPath =
      prefix.concat("-docs_and_externals.shard.scip");
  StdPath forwardDeclsOutputPath = prefix.concat("-forward_decls.shard.scip");
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
                   IndexJobResult{.kind = IndexJob::Kind::EmitIndex,
                                  .emitIndex = std::move(emitIndexResult)});
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
  using Status = Worker::ReceiveStatus;

  if (this->options.mode == WorkerMode::Compdb) {
    if (this->commandIndex >= this->compileCommands.size()) {
      return Status::Shutdown;
    }
    request.id = JobId::newTask(this->commandIndex);
    auto &command = this->compileCommands[this->commandIndex];
    ++this->commandIndex;
    request.job =
        IndexJob{.kind = IndexJob::Kind::SemanticAnalysis,
                 .semanticAnalysis = SemanticAnalysisJobDetails{command}};
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
  spdlog::debug("received job {}", request.id.debugString());
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
