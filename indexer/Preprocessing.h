#ifndef SCIP_CLANG_PREPROCESSING_H
#define SCIP_CLANG_PREPROCESSING_H

#include <functional>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/YAMLTraits.h"

#include "indexer/CliOptions.h" // for HeaderFilter
#include "indexer/Enforce.h"
#include "indexer/Hash.h"
#include "indexer/Indexer.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"

namespace clang {
class Token;
class MacroDefinition;
class MacroDirective;
} // namespace clang

namespace scip_clang {
struct SemanticAnalysisJobResult;
class ClangIdLookupMap;
} // namespace scip_clang

namespace scip_clang {

struct PreprocessorHistoryRecorder {
  HeaderFilter filter;
  llvm::yaml::Output yamlStream;
  std::function<llvm::StringRef(llvm::StringRef)> normalizePath;
};

struct IndexerPreprocessorOptions {
  RootPath projectRootPath;

  // Debugging-related
  PreprocessorHistoryRecorder *recorder;

  // Sort for deterministic output while running the preprocessor.
  bool deterministic;
};

struct HistoryEntry {
  llvm::yaml::Hex64 beforeHash;
  llvm::yaml::Hex64 afterHash;
  std::string mixedValue;
  std::string mixContext;
  std::string contextData;
};

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

struct HeaderInfoBuilder final {
  HashValueBuilder hashValueBuilder;
  const clang::FileID fileId;
};

class IndexerPreprocessorStack final {
  std::vector<std::optional<HeaderInfoBuilder>> state;

public:
  bool empty() const {
    return this->state.empty();
  }

  size_t size() const {
    return this->state.size();
  }

  bool isTopValid() {
    ENFORCE(!this->empty());
    return this->state.back().has_value();
  }

  HashValueBuilder &topHash() {
    ENFORCE(this->isTopValid());
    return this->state.back()->hashValueBuilder;
  }

  void popInvalid() {
    ENFORCE(!this->isTopValid());
    if (!this->state.empty()) {
      this->state.pop_back();
    }
  }

  std::optional<HeaderInfoBuilder> tryPopValid() {
    if (this->state.empty()) {
      return {};
    }
    ENFORCE(this->isTopValid());
    auto info = std::move(this->state.back());
    this->state.pop_back();
    return info;
  }

  void pushInvalid() {
    this->state.push_back(std::nullopt);
  }

  void pushValid(HeaderInfoBuilder &&info) {
    this->state.emplace_back(std::move(info));
  }

  std::string debugToString(const clang::SourceManager &sourceManager) const;
};

struct PreprocessorDebugContext {
  std::string tuMainFilePath;
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
                             PreprocessorDebugContext &&debugContext);

  void flushState(SemanticAnalysisJobResult &result,
                  ClangIdLookupMap &clangIdLookupMap,
                  MacroIndexer &macroIndexerOutput);

private:
  void enterFile(clang::SourceLocation sourceLoc);
  void enterFileImpl(bool recordHistory, clang::FileID enteredFileId);
  void exitFile(clang::FileID previousFileId);
  std::optional<HashValue> exitFileImpl(clang::FileID fileId);
  std::string pathKeyForHistory(clang::FileID fileId);

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
              clang::FileID previousFileId = clang::FileID()) override;

  virtual void
  MacroDefined(const clang::Token &macroNameToken,
               const clang::MacroDirective *macroDirective) override;

  virtual void MacroUndefined(const clang::Token &macroNameToken,
                              const clang::MacroDefinition &macroDefinition,
                              const clang::MacroDirective *) override {
    // FIXME: Mix the undef into the running hash
    this->macroIndexer.saveReference(macroNameToken, macroDefinition);
  }

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
      clang::SrcMgr::CharacteristicKind /*fileType*/) override;

  // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/21):
  // Add overrides for
  // - If
  // - Elif

  // END overrides from PPCallbacks
};

} // namespace scip_clang

#endif // SCIP_CLANG_PREPROCESSING_H