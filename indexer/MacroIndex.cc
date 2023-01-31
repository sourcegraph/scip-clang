#include <string_view>

#include "spdlog/fmt/fmt.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"
#include "indexer/MacroIndex.h"
#include "indexer/Path.h"

namespace scip_clang {

void FileLocalSourceRange::addToOccurrence(scip::Occurrence &occ) const {
  occ.add_range(this->startLine - 1);
  occ.add_range(this->startColumn - 1);
  if (this->startLine != this->endLine) {
    occ.add_range(this->endLine - 1);
  }
  occ.add_range(this->endColumn - 1);
}

std::string FileLocalSourceRange::debugToString() const {
  return fmt::format("{}:{}-{}:{}", this->startLine, this->startColumn,
                     this->endLine, this->endColumn);
}

std::string_view SymbolInterner::getMacroSymbol(clang::SourceLocation defLoc) {
  auto it = this->cache.find({defLoc});
  if (it != this->cache.end()) {
    return std::string_view(it->second);
  }
  auto defPLoc = this->sourceManager.getPresumedLoc(defLoc);
  ENFORCE(defPLoc.isValid());
  std::string_view filename;
  if (auto optRelPath = this->getCanonicalPath(defPLoc.getFileID())) {
    filename = optRelPath->asStringView();
  } else {
    filename = std::string_view(defPLoc.getFilename());
  }
  std::string out{fmt::format("c . todo-pkg todo-version {}:{}:{}#", filename,
                              defPLoc.getLine(), defPLoc.getColumn())};
  auto [newIt, inserted] = this->cache.insert({{defLoc}, std::move(out)});
  ENFORCE(inserted, "key was missing earlier, so insert should've succeeded");
  return std::string_view(newIt->second);
}

FileLocalMacroOccurrence::FileLocalMacroOccurrence(
    const clang::SourceManager &sourceManager, const clang::Token &macroToken,
    const clang::MacroInfo *defInfo, Role role)
    : defInfo(defInfo), role(role) {
  auto startLoc = sourceManager.getPresumedLoc(macroToken.getLocation());
  auto endLoc = sourceManager.getPresumedLoc(macroToken.getEndLoc());
  ENFORCE(startLoc.isValid());
  ENFORCE(endLoc.isValid());
  this->range = {startLoc.getLine(), startLoc.getColumn(), endLoc.getLine(),
                 endLoc.getColumn()};
  ENFORCE(startLoc.getLine() != endLoc.getLine()
              || startLoc.getColumn() != endLoc.getColumn(),
          "Found zero-length range for macro token: {}; getLocation() == "
          "getEndLoc() = {}; length = {}",
          debug::formatLoc(sourceManager, macroToken.getLocation()),
          macroToken.getLocation() == macroToken.getEndLoc(),
          macroToken.getLength());
  // Q: What source range does the defInfo store?
}

void FileLocalMacroOccurrence::saveOccurrence(SymbolInterner &symbolBuilder,
                                              scip::Occurrence &occ) const {
  switch (this->role) {
  case Role::Definition:
    occ.set_symbol_roles(scip::SymbolRole::Definition);
    occ.set_syntax_kind(scip::SyntaxKind::IdentifierMacroDefinition);
    break;
  case Role::Reference:
    occ.set_syntax_kind(scip::SyntaxKind::IdentifierMacro);
    break;
  }
  this->range.addToOccurrence(occ);
  auto name = symbolBuilder.getMacroSymbol(this->defInfo->getDefinitionLoc());
  occ.set_symbol(name.data(), name.size());
}

void FileLocalMacroOccurrence::saveScipSymbol(
    const std::string &name, scip::SymbolInformation &symbolInfo) const {
  symbolInfo.set_symbol(name);
  // TODO: Set documentation
}

void NonFileBasedMacro::saveScipSymbol(
    SymbolInterner &symbolBuilder, scip::SymbolInformation &symbolInfo) const {
  auto name = symbolBuilder.getMacroSymbol(this->defInfo->getDefinitionLoc());
  symbolInfo.set_symbol(name.data(), name.size());
}

void MacroIndex::saveOccurrence(clang::FileID occFileId,
                                const clang::Token &macroToken,
                                const clang::MacroInfo *macroInfo, Role role) {
  ENFORCE(occFileId.isValid(),
          "trying to record occurrence outside an actual file");
  ENFORCE(macroInfo, "missing macroInfo for definition of occurrence");
  // Deliberately using [..] for default initialization.
  if (macroToken.getLength() == 0) {
    llvm::errs() << "macroInfo for zero-length token = ";
    macroInfo->dump();
  }
  this->table[{occFileId}].emplace_back(FileLocalMacroOccurrence{
      *this->sourceManager, macroToken, macroInfo, role});
  // TODO: Do we need special handling for file-based macros
  // vs object-like macros?
}

/// Pre-condition: \param macroInfo is non-null.
void MacroIndex::saveNonFileBasedMacro(const clang::MacroInfo *macroInfo) {
  this->nonFileBasedMacros.insert({macroInfo});
}

void MacroIndex::saveDefinition(const clang::Token &macroNameToken,
                                const clang::MacroInfo *macroInfo) {
  ENFORCE(macroInfo);
  auto startPLoc =
      this->sourceManager->getPresumedLoc(macroInfo->getDefinitionLoc());
  ENFORCE(startPLoc.isValid());
  if (startPLoc.getFileID().isInvalid()) {
    this->saveNonFileBasedMacro(macroInfo);
    return;
  }
  this->saveOccurrence(startPLoc.getFileID(), macroNameToken, macroInfo,
                       Role::Definition);
}

void MacroIndex::saveReference(const clang::Token &macroNameToken,
                               const clang::MacroDefinition &macroDefinition) {
  if (macroDefinition.isAmbiguous()) { // buggy code? ignore for now
    return;
  }
  const clang::MacroInfo *defMacroInfo = macroDefinition.getMacroInfo();
  if (!defMacroInfo) {
    // E.g. if you `#undef cake` without `#define cake`
    // Should we emit something else so that this shows up as not-defined
    // in the UI? Otherwise, someone might think the precise intel
    // isn't working.
    return;
  }
  auto refLoc = macroNameToken.getLocation();
  if (!refLoc.isFileID()) {
    // TODO: When does this case arise/what should we do?
    return;
  }
  if (defMacroInfo->isBuiltinMacro()) {
    // E.g. __has_include(...)
    return;
  }
  ENFORCE(defMacroInfo->getDefinitionLoc().isValid(),
          "invalid definition loc for reference at {}",
          debug::formatRange(*this->sourceManager, macroNameToken.getLocation(),
                             macroNameToken.getEndLoc()));

  auto refPLoc =
      this->sourceManager->getPresumedLoc(macroNameToken.getLocation());
  ENFORCE(refPLoc.isValid());
  auto refFileId = refPLoc.getFileID();
  // Don't emit references from built-ins to other built-ins
  if (refFileId.isInvalid()) {
    // See NOTE(ref: macro-definition): This reference must be present in
    // the body of pre-defined macro (i.e. in the <built-in> header).
    // So it's not part of our project; don't emit a reference.
    return;
  }
  this->saveOccurrence(refFileId, macroNameToken, defMacroInfo,
                       Role::Reference);
}

void MacroIndex::emitDocumentOccurrencesAndSymbols(
    bool deterministic, SymbolInterner &symbolBuilder, clang::FileID fileId,
    scip::Document &document) {
  auto it = this->table.find({fileId});
  if (it == this->table.end()) {
    return;
  }
  std::string message{};
  ENFORCE(([&]() -> bool {
            absl::flat_hash_set<FileLocalMacroOccurrence> occSet;
            for (auto &macroOcc : it->second) {
              auto [setIt, inserted] = occSet.insert(macroOcc);
              if (!inserted) {
                message = fmt::format("found duplicate occurrence for {} in {}",
                                      macroOcc.range.debugToString(),
                                      document.relative_path());
                return false;
              }
            }
            return true;
          })(),
          "failed: {}", message);

  // Always deterministic as occurrences are stored deterministically
  // based on pre-processor traversal/actions.
  static_assert(std::is_same<decltype(it->second),
                             std::vector<FileLocalMacroOccurrence>>::value);
  (void)deterministic;
  for (auto &macroOcc : it->second) {
    scip::Occurrence occ;
    macroOcc.saveOccurrence(symbolBuilder, occ);
    switch (macroOcc.role) {
    case Role::Definition: {
      scip::SymbolInformation symbolInfo;
      ENFORCE(!occ.symbol().empty())
      macroOcc.saveScipSymbol(occ.symbol(), symbolInfo);
      *document.add_symbols() = std::move(symbolInfo);
      break;
    }
    case Role::Reference:
      break;
    }
    *document.add_occurrences() = std::move(occ);
  }
}

void MacroIndex::emitExternalSymbols(bool deterministic,
                                     SymbolInterner &symbolBuilder,
                                     scip::Index &index) {
  scip_clang::extractTransform(
      std::move(this->nonFileBasedMacros), deterministic,
      absl::FunctionRef<void(NonFileBasedMacro &&)>(
          [&](auto &&nonFileBasedMacro) -> void {
            scip::SymbolInformation symbolInfo;
            nonFileBasedMacro.saveScipSymbol(symbolBuilder, symbolInfo);
            *index.add_external_symbols() = std::move(symbolInfo);
          }));
}

// NOTE(def: macro-definitions)
// Macros can be defined in 4 different ways:
// 1. Builtin macros: This corresponds to "magical" macros
//    like __LINE__ etc. which cannot really be implemented in code itself.
//    For this macro type, MacroInfo::isBuiltinMacro() returns true.
//    isBuiltinMacro() returns false for all other macro types.
// 2. Pre-defined macros: This corresponds to macros coming from the
//    <built-in> magic header inside Clang.
//    https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/lib/Basic/Builtins.cpp
//    https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/include/clang/Basic/Builtins.def
//    For this macro type,
// 3. CLI-defined macros: E.g. -DQUEEN=IU.
// 4. Source-defined macros: These are normally added using #define,
//    but one can also use '#pragma (push|pop)_macro("macro_name")'
//    See https://gcc.gnu.org/onlinedocs/gcc/Push_002fPop-Macro-Pragmas.html

} // namespace scip_clang