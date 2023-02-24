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

#include "clang/AST/RecursiveASTVisitor.h"
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

  MacroIndexer macroIndexer;

  const PreprocessorDebugContext debugContext;

public:
  IndexerPreprocessorWrapper(clang::SourceManager &sourceManager,
                             const IndexerPreprocessorOptions &options,
                             PreprocessorDebugContext &&debugContext)
      : options(options), sourceManager(sourceManager), stack(),
        finishedProcessing(), finishedProcessingMulti(),
        macroIndexer(sourceManager), debugContext(std::move(debugContext)) {}

  void flushState(SemanticAnalysisJobResult &result, PathToIdMap &pathToIdMap,
                  MacroIndexer &macroIndexerOutput) {
    // HACK: It seems like EnterInclude and ExitInclude events are not
    // perfectly balanced in Clang. Work around that.
    auto lastEntry = this->stack.pop();
    ENFORCE(lastEntry.has_value());
    if (lastEntry->fileId == this->sourceManager.getMainFileID()) {
      // Already pre-processed files have yet another unbalanced entry
      auto newLastEntry = this->stack.pop();
      ENFORCE(newLastEntry.has_value());
      lastEntry.emplace(std::move(newLastEntry.value()));
    }
    ENFORCE(this->sourceManager.getFileEntryForID(lastEntry->fileId) == nullptr,
            "carelessly popped entry for '{}' without exiting",
            debug::tryGetPath(this->sourceManager, lastEntry->fileId));
    this->exitFile(this->sourceManager.getMainFileID());
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
          result.illBehavedFiles.emplace_back(PreprocessedFileInfoMulti{
              AbsolutePath{absPathRef}, std::move(hashes)});
          pathToIdMap.insert({absPathRef, fileId});
        }
      }
      if (this->options.deterministic) {
        absl::c_sort(result.illBehavedFiles);
      }
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
                        toStringView(recorder->normalizePath(path)), "",
                        "self path");
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

  // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/21):
  // Add overrides for
  // - If
  // - Elif
  // - InclusionDirective

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
using FilesToBeIndexedSet =
    absl::flat_hash_set<LlvmToAbslHashAdapter<clang::FileID>>;

/// Type to track canonical relative paths for _every_ FileID.
///
/// Two types of files may not have canonical relative paths:
/// 1. Paths inside the build root without a corresponding file inside
///    the project root.
/// 2. Files from outside the project.
class CanonicalPathMap final {
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>,
                      std::variant<RootRelativePathRef, AbsolutePathRef>>
      map;

  const RootPath &projectRootPath;

  const RootPath &buildRootPath;

public:
  CanonicalPathMap() = delete;
  CanonicalPathMap(const RootPath &projectRootPath,
                   const RootPath &buildRootPath)
      : map(), projectRootPath(projectRootPath), buildRootPath(buildRootPath) {}
  CanonicalPathMap(CanonicalPathMap &&other) = default;
  CanonicalPathMap &operator=(CanonicalPathMap &&) = delete;
  CanonicalPathMap(const CanonicalPathMap &) = delete;
  CanonicalPathMap &operator=(const CanonicalPathMap &) = delete;

  void populate(const PathToIdMap &pathToIdMap) {
    this->map.reserve(pathToIdMap.size());
    for (auto [absPathRef, fileId] : pathToIdMap) {
      this->insert(fileId, absPathRef);
    }
  }

  /// Returns true iff a new entry was inserted.
  void insert(clang::FileID fileId, AbsolutePathRef absPathRef) {
    ENFORCE(fileId.isValid(),
            "invalid FileIDs should be filtered out after preprocessing");
    ENFORCE(!absPathRef.asStringView().empty(),
            "inserting file with empty absolute path");

    auto insertRelPath = [&](RootRelativePathRef projectRootRelPath) -> void {
      ENFORCE(!projectRootRelPath.asStringView().empty(),
              "file path is unexpectedly equal to project root");
      this->map.insert({{fileId}, projectRootRelPath});
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
    this->map.insert({{fileId}, absPathRef});
  }

  bool contains(clang::FileID fileId) const {
    return this->map.contains({fileId});
  }

  /// See the doc comment on \c CanonicalPathMap
  std::optional<RootRelativePathRef>
  tryGetCanonicalPath(clang::FileID fileId) const {
    auto it = this->map.find({fileId});
    // TODO: Some headers like <built-in> and those in the Xcode SDK
    // are not getting populated here. That's fine, because they are
    // out-of-project anyways, but it seems weird that the populate(...)
    // call isn't adding them.
    if (it == this->map.end()) {
      return {};
    }
    if (std::holds_alternative<RootRelativePathRef>(it->second)) {
      return std::get<RootRelativePathRef>(it->second);
    }
    return {};
  }
};

struct PartialScipIndex {
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>, scip::Document>
      documents;
  std::vector<scip::SymbolInformation> externalSymbols;

  void insert(clang::FileID fileId, scip::Occurrence &&occ) {
    auto &doc = this->documents[{fileId}];
    *doc.add_occurrences() = std::move(occ);
  }
  void insert(clang::FileID fileId, scip::SymbolInformation &&symbolInfo) {
    auto &doc = this->documents[{fileId}];
    *doc.add_symbols() = std::move(symbolInfo);
  }
};

class IndexerAstVisitor : public clang::RecursiveASTVisitor<IndexerAstVisitor> {
  using Base = RecursiveASTVisitor;

  const CanonicalPathMap &pathMap;
  FilesToBeIndexedSet toBeIndexed;
  bool deterministic;

  TuIndexer &tuIndexer;

public:
  IndexerAstVisitor(const CanonicalPathMap &pathMap, FilesToBeIndexedSet &&map,
                    bool deterministic, TuIndexer &tuIndexer)
      : pathMap(pathMap), toBeIndexed(std::move(map)),
        deterministic(deterministic), tuIndexer(tuIndexer) {}

  // See clang/include/clang/Basic/DeclNodes.td for list of declarations.
  bool VisitNamespaceDecl(clang::NamespaceDecl *namespaceDecl) {
    this->tuIndexer.saveNamespaceDecl(namespaceDecl);
    return true;
  }

  void writeIndex(SymbolFormatter &&symbolFormatter, MacroIndexer &&macroIndex,
                  scip::Index &scipIndex) {
    std::vector<std::pair<RootRelativePathRef, clang::FileID>> relativePaths;
    for (auto wrappedFileId : toBeIndexed) {
      // We may have been asked to index files outside the project,
      // which lack canonical paths.
      if (auto optRelPathRef =
              pathMap.tryGetCanonicalPath(wrappedFileId.data)) {
        relativePaths.push_back({optRelPathRef.value(), wrappedFileId.data});
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
      absl::c_sort(relativePaths, comparePaths);
    }

    for (auto [relPathRef, fileId] : relativePaths) {
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
      *scipIndex.add_documents() = std::move(document);
    }
    macroIndex.emitExternalSymbols(this->deterministic, symbolFormatter,
                                   scipIndex);
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
  scip::Index &scipIndex;
  PartialScipIndex partialIndex;

public:
  IndexerAstConsumer(clang::CompilerInstance &, llvm::StringRef /*filepath*/,
                     const IndexerAstConsumerOptions &options,
                     IndexerPreprocessorWrapper *preprocessorWrapper,
                     scip::Index &scipIndex)
      : options(options), preprocessorWrapper(preprocessorWrapper),
        sema(nullptr), scipIndex(scipIndex), partialIndex() {}

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
    auto &sourceManager = astContext.getSourceManager();
    MacroIndexer macroIndexer{sourceManager};
    this->preprocessorWrapper->flushState(semaResult, pathToIdMap,
                                          macroIndexer);

    EmitIndexJobDetails emitIndexDetails{};
    bool shouldEmitIndex = this->options.getEmitIndexDetails(
        std::move(semaResult), emitIndexDetails);
    if (!shouldEmitIndex) {
      return;
    }

    CanonicalPathMap canonicalPathMap{this->options.projectRootPath,
                                      this->options.buildRootPath};
    FilesToBeIndexedSet toBeIndexed{};
    this->computePathsToBeIndexed(astContext, emitIndexDetails, pathToIdMap,
                                  canonicalPathMap, toBeIndexed);

    auto getRelativePath =
        [&](clang::FileID fileId) -> std::optional<RootRelativePathRef> {
      return canonicalPathMap.tryGetCanonicalPath(fileId);
    };
    SymbolFormatter symbolFormatter{sourceManager, getRelativePath};
    TuIndexer tuIndexer{sourceManager, this->sema->getLangOpts(),
                        symbolFormatter};

    IndexerAstVisitor visitor{canonicalPathMap, std::move(toBeIndexed),
                              this->options.deterministic, tuIndexer};
    visitor.TraverseAST(astContext);

    visitor.writeIndex(std::move(symbolFormatter), std::move(macroIndexer),
                       scipIndex);
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
                               CanonicalPathMap &canonicalPathMap,
                               FilesToBeIndexedSet &toBeIndexed) {
    auto &sourceManager = astContext.getSourceManager();
    auto mainFileId = sourceManager.getMainFileID();

    canonicalPathMap.populate(pathToIdMap);
    if (auto *mainFileEntry = sourceManager.getFileEntryForID(mainFileId)) {
      if (auto optMainFileAbsPath =
              AbsolutePathRef::tryFrom(mainFileEntry->tryGetRealPathName())) {
        canonicalPathMap.insert(mainFileId, optMainFileAbsPath.value());
        toBeIndexed.insert({mainFileId});
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
      toBeIndexed.insert({it->second});
      ENFORCE(canonicalPathMap.contains(it->second),
              "missing entry for path: {}", absPath.asStringRef());
    }
  }
};

class IndexerFrontendAction : public clang::ASTFrontendAction {
  const IndexerPreprocessorOptions &preprocessorOptions;
  const IndexerAstConsumerOptions &astConsumerOptions;
  scip::Index &scipIndex;

public:
  IndexerFrontendAction(const IndexerPreprocessorOptions &preprocessorOptions,
                        const IndexerAstConsumerOptions &astConsumerOptions,
                        scip::Index &scipIndex)
      : preprocessorOptions(preprocessorOptions),
        astConsumerOptions(astConsumerOptions), scipIndex(scipIndex) {}

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
        preprocessorWrapper, this->scipIndex);
  }
};

class IndexerFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
  const IndexerPreprocessorOptions &preprocessorOptions;
  const IndexerAstConsumerOptions &astConsumerOptions;
  scip::Index &scipIndex;

public:
  IndexerFrontendActionFactory(
      const IndexerPreprocessorOptions &preprocessorOptions,
      const IndexerAstConsumerOptions &astConsumerOptions,
      scip::Index &scipIndex)
      : preprocessorOptions(preprocessorOptions),
        astConsumerOptions(astConsumerOptions), scipIndex(scipIndex) {}

  virtual std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<IndexerFrontendAction>(
        this->preprocessorOptions, this->astConsumerOptions, this->scipIndex);
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
  RootPath projectRootPath{
      AbsolutePath{std::filesystem::current_path().string()},
      RootKind::Project};
  WorkerMode mode;
  IpcOptions ipcOptions;
  StdPath compdbPath;
  StdPath indexOutputPath{};
  if (cliOptions.workerMode == "ipc") {
    mode = WorkerMode::Ipc;
    ipcOptions = cliOptions.ipcOptions();
  } else if (cliOptions.workerMode == "compdb") {
    mode = WorkerMode::Compdb;
    compdbPath = StdPath(cliOptions.compdbPath);
    indexOutputPath = StdPath(cliOptions.indexOutputPath);
  } else {
    ENFORCE(cliOptions.workerMode == "testing");
    mode = WorkerMode::Testing;
  }
  return WorkerOptions{projectRootPath,
                       mode,
                       ipcOptions,
                       compdbPath,
                       indexOutputPath,
                       cliOptions.logLevel,
                       cliOptions.deterministic,
                       PreprocessorHistoryRecordingOptions{
                           cliOptions.preprocessorRecordHistoryFilterRegex,
                           cliOptions.preprocessorHistoryLogPath, false, ""},
                       cliOptions.temporaryOutputDir,
                       cliOptions.workerFault};
}

Worker::Worker(WorkerOptions &&options)
    : options(std::move(options)), messageQueues(), compileCommands(),
      commandIndex(0), recorder() {
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
    parser.initialize(compdbFile, std::numeric_limits<size_t>::max());
    parser.parseMore(this->compileCommands);
    std::fclose(compdbFile.file);
    break;
  }
  case WorkerMode::Testing:
    break;
  }

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
      preprocessorOptions, astConsumerOptions, scipIndex);

  clang::tooling::ToolInvocation invocation(
      std::move(args), &frontendActionFactory, fileManager.get(),
      std::make_shared<clang::PCHContainerOperations>());

  IndexerDiagnosticConsumer diagnosticConsumer;
  invocation.setDiagnosticConsumer(&diagnosticConsumer);

  {
    LogTimerRAII timer(fmt::format("invocation for {}", job.command.Filename));
    bool ranSuccessfully = invocation.run();
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
  ENFORCE(this->options.mode == WorkerMode::Ipc);
  this->messageQueues->workerToDriver.send(IndexJobResponse{
      this->ipcOptions().workerId, requestId, std::move(result)});
  this->flushStreams();
}

Worker::ReceiveStatus Worker::processTranslationUnitAndRespond(
    IndexJobRequest &&semanticAnalysisRequest) {
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
      for (auto &p : semaResult.wellBehavedFiles) {
        emitIndexDetails.filesToBeIndexed.emplace_back(std::move(p.path));
      }
      for (auto &p : semaResult.illBehavedFiles) {
        emitIndexDetails.filesToBeIndexed.emplace_back(std::move(p.path));
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
  scip::Index scipIndex{};
  auto &semaDetails = semanticAnalysisRequest.job.semanticAnalysis;

  scip_clang::exceptionContext =
      fmt::format("processing {}", semaDetails.command.Filename);
  this->processTranslationUnit(std::move(semaDetails), callback, scipIndex);
  scip_clang::exceptionContext = "";

  ENFORCE(callbackInvoked == 1,
          "callbackInvoked = {} for TU with main file '{}'", callbackInvoked,
          tuMainFilePath);
  if (innerStatus != ReceiveStatus::OK) {
    return innerStatus;
  }

  StdPath outputPath = this->options.mode == WorkerMode::Compdb
                           ? this->options.indexOutputPath
                           : (this->options.temporaryOutputDir
                              / fmt::format("job-{}-worker-{}.index.scip",
                                            emitIndexRequestId.taskId(),
                                            this->ipcOptions().workerId));
  this->emitIndex(std::move(scipIndex), outputPath);

  if (this->options.mode == WorkerMode::Compdb) {
    return ReceiveStatus::OK;
  }

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
                  scip_clang::formatLlvm(recvError));
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
