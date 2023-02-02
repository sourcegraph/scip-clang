#ifndef SCIP_CLANG_MACRO_INDEX_H
#define SCIP_CLANG_MACRO_INDEX_H

#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Token.h"

#include "scip/scip.pb.h"

#include "indexer/LLVMAdapter.h"
#include "indexer/Path.h"

namespace scip_clang {

struct FileLocalSourceRange {
  uint32_t startLine;   // 1-based
  uint32_t startColumn; // 1-based
  uint32_t endLine;     // 1-based
  uint32_t endColumn;   // 1-based

  template <typename H>
  friend H AbslHashValue(H h, const FileLocalSourceRange &r) {
    return H::combine(std::move(h), r.startLine, r.startColumn, r.endLine,
                      r.endLine);
  }
  DERIVE_EQ_ALL(FileLocalSourceRange);

  void addToOccurrence(scip::Occurrence &occ) const;

  std::string debugToString() const;
};

using GetCanonicalPath =
    absl::FunctionRef<std::optional<RootRelativePathRef>(clang::FileID)>;

class SymbolInterner final {
  const clang::SourceManager &sourceManager;
  GetCanonicalPath getCanonicalPath;

  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::SourceLocation>, std::string>
      cache;

public:
  SymbolInterner(const clang::SourceManager &sourceManager,
                 GetCanonicalPath getCanonicalPath)
      : sourceManager(sourceManager), getCanonicalPath(getCanonicalPath),
        cache() {}
  SymbolInterner(const SymbolInterner &) = delete;
  SymbolInterner &operator=(const SymbolInterner &) = delete;

  std::string_view getMacroSymbol(clang::SourceLocation defLoc);
};

enum class Role {
  Definition,
  Reference,
};

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

  void saveOccurrence(SymbolInterner &symbolBuilder,
                      scip::Occurrence &out) const;

  void saveScipSymbol(const std::string &name,
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

  void saveScipSymbol(SymbolInterner &symbolBuilder,
                      scip::SymbolInformation &symbolInfo) const;
};

class MacroIndex final {
  clang::SourceManager *sourceManager; // non-null
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>,
                      std::vector<FileLocalMacroOccurrence>>
      table;

  absl::flat_hash_set<NonFileBasedMacro> nonFileBasedMacros;

public:
  MacroIndex(clang::SourceManager &m)
      : sourceManager(&m), table(), nonFileBasedMacros() {}
  MacroIndex(MacroIndex &&) = default;
  MacroIndex &operator=(MacroIndex &&other) = default;
  MacroIndex(const MacroIndex &) = delete;
  MacroIndex &operator=(const MacroIndex &&) = delete;

  void saveReference(const clang::Token &macroNameToken,
                     const clang::MacroDefinition &);

  void saveDefinition(const clang::Token &macroNameToken,
                      const clang::MacroInfo *);

  void emitDocumentOccurrencesAndSymbols(bool deterministic, SymbolInterner &,
                                         clang::FileID, scip::Document &);
  void emitExternalSymbols(bool deterministic, SymbolInterner &, scip::Index &);

private:
  /// Pre-condition: all arguments are valid/non-null.
  void saveOccurrence(clang::FileID occFileId, const clang::Token &macroToken,
                      const clang::MacroInfo *, Role);

  /// Pre-condition: \param macroInfo is non-null.
  void saveNonFileBasedMacro(const clang::MacroInfo *macroInfo);
};

} // namespace scip_clang

#endif // SCIP_CLANG_MACRO_INDEX_H