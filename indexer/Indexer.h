#ifndef SCIP_CLANG_MACRO_INDEX_H
#define SCIP_CLANG_MACRO_INDEX_H

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Token.h"

#include "scip/scip.pb.h"

#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"
#include "indexer/ScipExtras.h"
#include "indexer/SymbolFormatter.h"

namespace clang {
class NamespaceDecl;
} // namespace clang

namespace scip_clang {

// Denotes an inclusive source range within a file with 1-based offsets.
//
// This matches up with Clang APIs like getLoc(), getEndLoc() etc.
// so extra +1/-1 calculations should generally not be needed.
struct FileLocalSourceRange {
  uint32_t startLine;
  uint32_t startColumn;
  uint32_t endLine;
  uint32_t endColumn;

  template <typename H>
  friend H AbslHashValue(H h, const FileLocalSourceRange &r) {
    return H::combine(std::move(h), r.startLine, r.startColumn, r.endLine,
                      r.endLine);
  }
  DERIVE_EQ_ALL(FileLocalSourceRange);

  static std::pair<FileLocalSourceRange, clang::FileID>
  fromNonEmpty(const clang::SourceManager &, clang::SourceRange inclusiveRange);

  void addToOccurrence(scip::Occurrence &occ) const;

  std::string debugToString() const;
};

enum class Role {
  Definition,
  Reference,
};

// NOTE(def: emit-vs-save)
// - Use 'emit' for methods recording information in a scip::XYZ type,
//   generally passed in as an output parameter.
// - Use 'save' for methods which transform and record parts of
//   of the parameters `this` (to be emitted later).

struct FileLocalMacroOccurrence {
  FileLocalSourceRange range;
  const clang::MacroInfo *defInfo; // always points to the definition
  Role role;

  DERIVE_HASH_1(FileLocalMacroOccurrence, self.range)

  friend bool operator==(const FileLocalMacroOccurrence &m1,
                         const FileLocalMacroOccurrence &m2) {
    return m1.range == m2.range;
  }

  FileLocalMacroOccurrence(const clang::SourceManager &sourceManager,
                           const clang::Token &macroToken,
                           const clang::MacroInfo *defInfo, Role);

  // See NOTE(ref: emit-vs-save) on naming conventions.

  void emitOccurrence(SymbolFormatter &, scip::Occurrence &out) const;

  void emitSymbolInformation(const std::string &name,
                             scip::SymbolInformation &symbolInfo) const;
};

// See NOTE(ref: macro-definitions). This covers all macros which are
// not defined in the source code.
struct NonFileBasedMacro {
  // ASSUMPTION: MacroInfo values are interned and comparing using
  // pointer equality is correct.
  const clang::MacroInfo *defInfo;

  DERIVE_HASH_1(NonFileBasedMacro, reinterpret_cast<const void *>(self.defInfo))
  DERIVE_EQ_ALL(NonFileBasedMacro)

  friend std::strong_ordering operator<=>(const NonFileBasedMacro &m1,
                                          const NonFileBasedMacro &m2) {
    // ASSUMPTION: built-in definitions must be in the same "header"
    // so the relative position should be deterministic
    return m1.defInfo->getDefinitionLoc().getRawEncoding()
           <=> m2.defInfo->getDefinitionLoc().getRawEncoding();
  }

  void emitSymbolInformation(SymbolFormatter &symbolFormatter,
                             scip::SymbolInformation &symbolInfo) const;
};

class MacroIndexer final {
  clang::SourceManager *sourceManager; // non-null
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>,
                      std::vector<FileLocalMacroOccurrence>>
      table;

  absl::flat_hash_set<NonFileBasedMacro> nonFileBasedMacros;

public:
  MacroIndexer(clang::SourceManager &m)
      : sourceManager(&m), table(), nonFileBasedMacros() {}
  MacroIndexer(MacroIndexer &&) = default;
  MacroIndexer &operator=(MacroIndexer &&other) = default;
  MacroIndexer(const MacroIndexer &) = delete;
  MacroIndexer &operator=(const MacroIndexer &&) = delete;

  // See NOTE(ref: emit-vs-save) for naming conventions.

  void saveReference(const clang::Token &macroNameToken,
                     const clang::MacroDefinition &);

  void saveDefinition(const clang::Token &macroNameToken,
                      const clang::MacroInfo *);

  void emitDocumentOccurrencesAndSymbols(bool deterministic, SymbolFormatter &,
                                         clang::FileID, scip::Document &);
  void emitExternalSymbols(bool deterministic, SymbolFormatter &,
                           scip::Index &);

private:
  /// Pre-condition: all arguments are valid/non-null.
  void saveOccurrence(clang::FileID occFileId, const clang::Token &macroToken,
                      const clang::MacroInfo *, Role);

  /// Pre-condition: \param macroInfo is non-null.
  void saveNonFileBasedMacro(const clang::MacroInfo *macroInfo);
};

/// Different from \c scip::DocumentBuilder because we can get away with
/// a vector of occurrences, since each occurrence will only be traversed
/// once. However, if this changes, say because we start emitting
/// references inside macro bodies (at each point of expansion), then
/// we may want to consider doing away with this type.
struct PartialDocument {
  std::vector<scip::OccurrenceExt> occurrences;
  // Keyed by the symbol name. The symbol name is not set on the
  // SymbolInformation value to avoid redundant allocations.
  absl::flat_hash_map<std::string_view, scip::SymbolInformation> symbolInfos;
};

class TuIndexer final {
  const clang::SourceManager &sourceManager;
  const clang::LangOptions &langOptions;
  SymbolFormatter &symbolFormatter;
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>, PartialDocument>
      documentMap;

public:
  TuIndexer(const clang::SourceManager &, const clang::LangOptions &,
            SymbolFormatter &);

  // See NOTE(ref: emit-vs-save) for naming conventions.

  void saveNamespaceDecl(const clang::NamespaceDecl *);

  void emitDocumentOccurrencesAndSymbols(bool deterministic, clang::FileID,
                                         scip::Document &);
};

} // namespace scip_clang

#endif // SCIP_CLANG_MACRO_INDEX_H