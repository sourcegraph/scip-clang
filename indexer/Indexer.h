#ifndef SCIP_CLANG_MACRO_INDEX_H
#define SCIP_CLANG_MACRO_INDEX_H

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "clang/AST/RawCommentList.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

#include "indexer/Comparison.h"
#include "indexer/Derive.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"
#include "indexer/ScipExtras.h"
#include "indexer/SymbolFormatter.h"

#define FOR_EACH_EXPR_TO_BE_INDEXED(F) \
  F(DeclRef)                           \
  F(Member)

#define FOR_EACH_TYPE_TO_BE_INDEXED(F) \
  F(Enum)                              \
  F(Record)

namespace clang {
#define FORWARD_DECLARE(DeclName) class DeclName##Decl;
FOR_EACH_DECL_TO_BE_INDEXED(FORWARD_DECLARE)
#undef FORWARD_DECLARE

#define FORWARD_DECLARE(ExprName) class ExprName##Expr;
FOR_EACH_EXPR_TO_BE_INDEXED(FORWARD_DECLARE)
#undef FORWARD_DECLARE

#define FORWARD_DECLARE(TypeName) class TypeName##TypeLoc;
FOR_EACH_TYPE_TO_BE_INDEXED(FORWARD_DECLARE)
#undef FORWARD_DECLARE

class ASTContext;
class Decl;
class MacroDefinition;
class MacroInfo;
class NestedNameSpecifierLoc;
class SourceManager;
class TagDecl;
class TagTypeLoc;
class Token;
} // namespace clang

namespace scip {
class Document;
class Index;
class Occurrence;
class SymbolInformation;
} // namespace scip

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
  DERIVE_CMP_ALL(FileLocalSourceRange)
  DERIVE_EQ_ALL(FileLocalSourceRange)

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

  // OK because we do not expect different code paths to emit occurrences
  // with different roles or different macros for the same ranges.
  DERIVE_HASH_CMP_NEWTYPE(FileLocalMacroOccurrence, range, CMP_EXPR)

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
                                          const NonFileBasedMacro &m2);

  void emitSymbolInformation(SymbolFormatter &symbolFormatter,
                             scip::SymbolInformation &symbolInfo) const;
};

class MacroIndexer final {
  clang::SourceManager *sourceManager; // non-null

  // Information about all the macro occurrences grouped by file
  //
  // The value is a set of occurrences rather than a vector,
  // to avoid memory usage growing for highly used macros. For example,
  //
  //   (1) #define A 0
  //   (2) #define A2 (2 * A)
  //   (3) int a4 = A2 * A2;
  //
  // When expanding each reference to A2 on line 3, we will also
  // see an occurrence of 'A' on line 2. The set de-duplicates those.
  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>,
                      absl::flat_hash_set<FileLocalMacroOccurrence>>
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
  [[maybe_unused]] const clang::ASTContext &astContext;
  SymbolFormatter &symbolFormatter;
  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>, PartialDocument>
      documentMap;

public:
  TuIndexer(const clang::SourceManager &, const clang::LangOptions &,
            const clang::ASTContext &, SymbolFormatter &);

  // See NOTE(ref: emit-vs-save) for naming conventions.
#define SAVE_DECL(DeclName) \
  void save##DeclName##Decl(const clang::DeclName##Decl &);
  FOR_EACH_DECL_TO_BE_INDEXED(SAVE_DECL)
#undef SAVE_DECL

  void saveTagDecl(const clang::TagDecl &);
  void saveTagTypeLoc(const clang::TagTypeLoc &);

#define SAVE_EXPR(ExprName) \
  void save##ExprName##Expr(const clang::ExprName##Expr &);
  FOR_EACH_EXPR_TO_BE_INDEXED(SAVE_EXPR)
#undef SAVE_EXPR
  void saveNestedNameSpecifierLoc(const clang::NestedNameSpecifierLoc &);

#define SAVE_TYPE_LOC(TypeName) \
  void save##TypeName##TypeLoc(const clang::TypeName##TypeLoc &);
  FOR_EACH_TYPE_TO_BE_INDEXED(SAVE_TYPE_LOC)
#undef SAVE_TYPE_LOC

  void emitDocumentOccurrencesAndSymbols(bool deterministic, clang::FileID,
                                         scip::Document &);

private:
  std::pair<FileLocalSourceRange, clang::FileID>
  getTokenExpansionRange(clang::SourceLocation startLoc) const;

  void saveReference(std::string_view symbol, clang::SourceLocation loc,
                     int32_t extraRoles = 0);

  /// Helper method for recording a \c scip::Occurrence and a
  /// \c scip::SymbolInformation for a definition.
  ///
  /// Setting the symbol name on \param symbolInfo is not necessary.
  ///
  /// For local variables, \param symbolInfo should be \c std::nullopt.
  void saveDefinition(std::string_view symbol, clang::SourceLocation loc,
                      std::optional<scip::SymbolInformation> &&symbolInfo,
                      int32_t extraRoles = 0);

  /// Lower-level method for only saving a Occurrence.
  ///
  /// Returns a \c PartialDocument reference so that \c scip::SymbolInformation
  /// can be added into the document for definitions. Prefer using
  /// \c saveDefinition or \c saveReference over this method.
  PartialDocument &saveOccurrence(std::string_view symbol,
                                  clang::SourceLocation loc,
                                  int32_t allRoles = 0);

  std::vector<clang::RawComment::CommentLine>
  tryGetDocComment(const clang::Decl &) const;
};

} // namespace scip_clang

#endif // SCIP_CLANG_MACRO_INDEX_H
