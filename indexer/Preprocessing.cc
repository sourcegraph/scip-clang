#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/MacroInfo.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/DebugHelpers.h"
#include "indexer/IdPathMappings.h"
#include "indexer/Indexer.h"
#include "indexer/IpcMessages.h"
#include "indexer/Path.h"
#include "indexer/Preprocessing.h"

namespace scip_clang {

std::string IndexerPreprocessorStack::debugToString(
    const clang::SourceManager &sourceManager) const {
  std::string buf = fmt::format("[{}]{{", this->state.size());
  llvm::raw_string_ostream os(buf);
  for (size_t i = 0; i < this->state.size(); ++i) {
    size_t j = this->state.size() - i - 1;
    if (auto &optBuilder = this->state[j]) {
      os << debug::tryGetPath(sourceManager, optBuilder->fileId);
    } else {
      os << "<invalid>";
    }
    if (j != 0) {
      os << ", ";
    }
  }
  os << "}";
  return buf;
}

IndexerPreprocessorWrapper::IndexerPreprocessorWrapper(
    clang::SourceManager &sourceManager,
    const IndexerPreprocessorOptions &options,
    PreprocessorDebugContext &&debugContext)
    : options(options), sourceManager(sourceManager), stack(),
      finishedProcessing(), macroIndexer(sourceManager),
      debugContext(std::move(debugContext)) {}

void IndexerPreprocessorWrapper::flushState(SemanticAnalysisJobResult &result,
                                            ClangIdLookupMap &clangIdLookupMap,
                                            MacroIndexer &macroIndexerOutput) {
  spdlog::debug("flushing preprocessor state");
  // FileChanged:EnterFile calls are almost exactly balanced by
  // FileChanged:ExitFile calls, except for the main file,
  // which is matched by an EndOfMainFile call.
  //
  // However, flushState is called before EndOfMainFile
  // (see NOTE(ref: preprocessor-traversal-ordering))
  // So make sure to clear the stack here.
  auto mainFileId = this->sourceManager.getMainFileID();
  this->exitFile(mainFileId);
  // Strictly speaking, we should ENFORCE(this->stack.empty()) here.
  // However, when running C-reduce, it can end up generating
  // reduced pre-processed files which do not correctly obey the
  // Enter-Exit pairing that is followed by hand-written
  // and pre-processed code. So don't assert it here.

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
          return;
        }
        ENFORCE(map.size() > 1, "clangIdLookupMap stores non-empty maps");
        std::vector<HashValue> hashes;
        hashes.reserve(map.size());
        for (auto &[hashValue, fileId] : map) {
          hashes.push_back(hashValue);
        }
        result.illBehavedFiles.emplace_back(PreprocessedFileInfoMulti{
            AbsolutePath{absPathRef}, std::move(hashes)});
      });
  if (this->options.deterministic) {
    absl::c_sort(result.wellBehavedFiles);
    absl::c_sort(result.illBehavedFiles);
  }
}

#define MIX_INTO_HASH(_stack, _value, _path_expr, _context_expr)          \
  do {                                                                    \
    if (_stack.isTopValid()) {                                            \
      auto &_hash = _stack.topHash();                                     \
      if (_hash.isRecordingHistory()) {                                   \
        _hash.mixWithContext(                                             \
            _value, HistoryEntry{.mixedValue = fmt::format("{}", _value), \
                                 .mixContext = _context_expr,             \
                                 .contextData = _path_expr});             \
      } else {                                                            \
        _hash.mix(_value);                                                \
      }                                                                   \
    }                                                                     \
  } while (0)

void IndexerPreprocessorWrapper::enterFile(clang::SourceLocation sourceLoc) {
  if (!sourceLoc.isValid()) {
    this->stack.pushInvalid();
    return;
  }
  ENFORCE(sourceLoc.isFileID(), "EnterFile called on a non-FileID");
  auto enteredFileId = this->sourceManager.getFileID(sourceLoc);
  if (!enteredFileId.isValid()) {
    this->stack.pushInvalid();
    return;
  }
  if (auto *recorder = this->options.recorder) {
    if (auto *enteredFileEntry =
            this->sourceManager.getFileEntryForID(enteredFileId)) {
      auto path = enteredFileEntry->tryGetRealPathName();
      if (!path.empty() && recorder->filter.matches(path)) {
        this->enterFileImpl(true, enteredFileId);
        MIX_INTO_HASH(this->stack,
                      llvm_ext::toStringView(recorder->normalizePath(path)), "",
                      "self path");
        return;
      }
    }
  }
  this->enterFileImpl(false, enteredFileId);
}

void IndexerPreprocessorWrapper::enterFileImpl(bool recordHistory,
                                               clang::FileID enteredFileId) {
  this->stack.pushValid(
      HeaderInfoBuilder{HashValueBuilder(recordHistory), enteredFileId});
}

void IndexerPreprocessorWrapper::exitFile(clang::FileID previousFileId) {
  auto optHash = this->exitFileImpl(previousFileId);
  if (!optHash || this->stack.empty()) {
    return;
  }
  MIX_INTO_HASH(this->stack, optHash->rawValue,
                this->pathKeyForHistory(previousFileId), "hash for #include");
}

namespace {
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

std::optional<HashValue>
IndexerPreprocessorWrapper::exitFileImpl(clang::FileID fileId) {
  if (fileId.isInvalid()) {
    if (this->stack.isTopValid()) {
      auto popped = this->stack.tryPopValid();
      ENFORCE(popped.has_value());
    } else {
      this->stack.popInvalid();
    }
    return {}; // No need to return a hash for an invalid file
  }
  auto optHeaderInfo = this->stack.tryPopValid();
  ENFORCE(optHeaderInfo.has_value(), "missing matching enterInclude for exit");
  auto fileInfo = std::move(optHeaderInfo.value());
  bool fileIdMatchesTopOfStack = fileInfo.fileId == fileId;
  if (!fileIdMatchesTopOfStack) {
    ENFORCE(fileIdMatchesTopOfStack,
            "fileId mismatch:\ntop of stack: {}\nexitInclude: {}",
            debug::tryGetPath(this->sourceManager, fileInfo.fileId),
            debug::tryGetPath(this->sourceManager, fileId));
  }

  auto [hashValue, history] = fileInfo.hashValueBuilder.finish();
  if (fileInfo.fileId.isValid()) {
    auto key = llvm_ext::AbslHashAdapter<clang::FileID>{fileInfo.fileId};
    auto it = this->finishedProcessing.find(key);
    if (it == this->finishedProcessing.end()) {
      this->finishedProcessing.insert({key, hashValue});
    }
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

std::string
IndexerPreprocessorWrapper::pathKeyForHistory(clang::FileID fileId) {
  ENFORCE(this->options.recorder);
  return this->options.recorder
      ->normalizePath(debug::tryGetPath(this->sourceManager, fileId))
      .str();
}

// virtual override
void IndexerPreprocessorWrapper::FileChanged(
    clang::SourceLocation sourceLoc,
    clang::PPCallbacks::FileChangeReason reason,
    clang::SrcMgr::CharacteristicKind /*fileType*/,
    clang::FileID previousFileId) {
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
    this->enterFile(sourceLoc);
    break;
  }
  }
}

// virtual override
void IndexerPreprocessorWrapper::MacroDefined(
    const clang::Token &macroNameToken,
    const clang::MacroDirective *macroDirective) {
  ENFORCE(macroDirective != nullptr);
  auto *macroInfo = macroDirective->getMacroInfo();
  this->macroIndexer.saveDefinition(macroNameToken, macroInfo);
  // FIXME: Mix the macro definition into the running hash
}

// virtual override
void IndexerPreprocessorWrapper::InclusionDirective(
    clang::SourceLocation /*hashLoc*/, const clang::Token & /*includeTok*/,
    llvm::StringRef /*fileName*/, bool /*isAngled*/,
    clang::CharSourceRange fileNameRange,
    clang::OptionalFileEntryRef optFileEntry, clang::StringRef /*searchPath*/,
    clang::StringRef /*relativePath*/, const clang::Module * /*importModule*/,
    clang::SrcMgr::CharacteristicKind /*fileType*/) {
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

} // namespace scip_clang