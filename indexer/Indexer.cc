#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "spdlog/fmt/fmt.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"
#include "indexer/Indexer.h"
#include "indexer/Path.h"
#include "indexer/ScipExtras.h"

namespace scip_clang {

std::pair<FileLocalSourceRange, clang::FileID>
FileLocalSourceRange::fromNonEmpty(const clang::SourceManager &sourceManager,
                                   clang::SourceRange inclusiveRange) {
  auto start = inclusiveRange.getBegin();
  auto end = inclusiveRange.getEnd();
  ENFORCE(start.isValid());
  ENFORCE(end.isValid());
  ENFORCE(start <= end, "called fromNonEmpty with empty range");
  auto fileId = sourceManager.getFileID(start);
  ENFORCE(sourceManager.getFileID(end) == fileId,
          "range should not be split across files");
  auto startPLoc = sourceManager.getPresumedLoc(start);
  auto endPLoc = sourceManager.getPresumedLoc(end);
  return {{startPLoc.getLine(), startPLoc.getColumn(), endPLoc.getLine(),
           endPLoc.getColumn()},
          fileId};
}

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

FileLocalMacroOccurrence::FileLocalMacroOccurrence(
    const clang::SourceManager &sourceManager, const clang::Token &macroToken,
    const clang::MacroInfo *defInfo, Role role)
    : defInfo(defInfo), role(role) {
  auto startLoc = sourceManager.getSpellingLoc(macroToken.getLocation());
  auto endLoc = startLoc.getLocWithOffset(macroToken.getLength());

  auto [range, _] =
      FileLocalSourceRange::fromNonEmpty(sourceManager, {startLoc, endLoc});
  this->range = range;
}

void FileLocalMacroOccurrence::emitOccurrence(SymbolFormatter &symbolFormatter,
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
  auto name = symbolFormatter.getMacroSymbol(this->defInfo->getDefinitionLoc());
  occ.set_symbol(name.data(), name.size());
}

void FileLocalMacroOccurrence::emitSymbolInformation(
    const std::string &name, scip::SymbolInformation &symbolInfo) const {
  symbolInfo.set_symbol(name);
  // TODO: Set documentation
}

std::strong_ordering operator<=>(const NonFileBasedMacro &m1,
                                 const NonFileBasedMacro &m2) {
  // ASSUMPTION: built-in definitions must be in the same "header"
  // so the relative position should be deterministic
  return m1.defInfo->getDefinitionLoc().getRawEncoding()
         <=> m2.defInfo->getDefinitionLoc().getRawEncoding();
}

void NonFileBasedMacro::emitSymbolInformation(
    SymbolFormatter &symbolFormatter,
    scip::SymbolInformation &symbolInfo) const {
  auto name = symbolFormatter.getMacroSymbol(this->defInfo->getDefinitionLoc());
  symbolInfo.set_symbol(name.data(), name.size());
}

void MacroIndexer::saveOccurrence(clang::FileID occFileId,
                                  const clang::Token &macroToken,
                                  const clang::MacroInfo *macroInfo,
                                  Role role) {
  ENFORCE(occFileId.isValid(),
          "trying to record occurrence outside an actual file");
  ENFORCE(macroInfo, "missing macroInfo for definition of occurrence");
  this->table[{occFileId}].insert(FileLocalMacroOccurrence{
      *this->sourceManager, macroToken, macroInfo, role});
  // TODO: Do we need special handling for file-based macros
  // vs object-like macros?
}

// NOTE(def: macro-definition)
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

/// Pre-condition: \param macroInfo is non-null.
void MacroIndexer::saveNonFileBasedMacro(const clang::MacroInfo *macroInfo) {
  this->nonFileBasedMacros.insert({macroInfo});
}

void MacroIndexer::saveDefinition(const clang::Token &macroNameToken,
                                  const clang::MacroInfo *macroInfo) {
  ENFORCE(macroInfo);
  auto fileId = this->sourceManager->getFileID(macroInfo->getDefinitionLoc());
  if (fileId.isInvalid()) {
    this->saveNonFileBasedMacro(macroInfo);
    return;
  }
  this->saveOccurrence(fileId, macroNameToken, macroInfo, Role::Definition);
}

void MacroIndexer::saveReference(
    const clang::Token &macroNameToken,
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
  if (defMacroInfo->isBuiltinMacro()) {
    // E.g. __has_include(...)
    return;
  }
  ENFORCE(defMacroInfo->getDefinitionLoc().isValid(),
          "invalid definition loc for reference at {}",
          debug::formatRange(*this->sourceManager, macroNameToken.getLocation(),
                             macroNameToken.getEndLoc()));

  auto refLoc = sourceManager->getSpellingLoc(macroNameToken.getLocation());
  auto refFileId = this->sourceManager->getFileID(refLoc);
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

void MacroIndexer::emitDocumentOccurrencesAndSymbols(
    bool deterministic, SymbolFormatter &symbolFormatter, clang::FileID fileId,
    scip::Document &document) {
  auto it = this->table.find({fileId});
  if (it == this->table.end()) {
    return;
  }
  std::string message{};
  scip_clang::extractTransform(
      std::move(it->second), deterministic,
      absl::FunctionRef<void(FileLocalMacroOccurrence &&)>(
          [&](auto &&macroOcc) {
            scip::Occurrence occ;
            macroOcc.emitOccurrence(symbolFormatter, occ);
            switch (macroOcc.role) {
            case Role::Definition: {
              scip::SymbolInformation symbolInfo;
              ENFORCE(!occ.symbol().empty())
              macroOcc.emitSymbolInformation(occ.symbol(), symbolInfo);
              *document.add_symbols() = std::move(symbolInfo);
              break;
            }
            case Role::Reference:
              break;
            }
            *document.add_occurrences() = std::move(occ);
          }));
}

void MacroIndexer::emitExternalSymbols(bool deterministic,
                                       SymbolFormatter &symbolFormatter,
                                       scip::Index &index) {
  scip_clang::extractTransform(
      std::move(this->nonFileBasedMacros), deterministic,
      absl::FunctionRef<void(NonFileBasedMacro &&)>(
          [&](auto &&nonFileBasedMacro) -> void {
            scip::SymbolInformation symbolInfo;
            nonFileBasedMacro.emitSymbolInformation(symbolFormatter,
                                                    symbolInfo);
            *index.add_external_symbols() = std::move(symbolInfo);
          }));
}

TuIndexer::TuIndexer(const clang::SourceManager &sourceManager,
                     const clang::LangOptions &langOptions,
                     SymbolFormatter &symbolFormatter)
    : sourceManager(sourceManager), langOptions(langOptions),
      symbolFormatter(symbolFormatter), documentMap() {}

void TuIndexer::saveEnumConstantDecl(
    const clang::EnumConstantDecl *enumConstantDecl) {
  ENFORCE(enumConstantDecl);
  auto optSymbol =
      this->symbolFormatter.getEnumConstantSymbol(enumConstantDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  auto symbol = optSymbol.value();

  ENFORCE(enumConstantDecl->getBeginLoc() == enumConstantDecl->getLocation());
  auto [range, fileId] =
      this->getTokenExpansionRange(enumConstantDecl->getLocation());

  scip::Occurrence occ;
  range.addToOccurrence(occ);
  occ.set_symbol(symbol.data(), symbol.size());
  occ.set_symbol_roles(scip::SymbolRole::Definition);

  auto &doc = this->documentMap[{fileId}];
  doc.occurrences.emplace_back(scip::OccurrenceExt{std::move(occ)});

  llvm::SmallVector<std::string, 4> docComments{};
  this->tryGetDocComment(enumConstantDecl, docComments);
  scip::SymbolInformation symbolInfo;
  for (auto &docComment : docComments) {
    *symbolInfo.add_documentation() = std::move(docComment);
  }
  doc.symbolInfos.emplace(symbol, std::move(symbolInfo));
}

void TuIndexer::saveEnumDecl(const clang::EnumDecl *enumDecl) {
  ENFORCE(enumDecl);
  auto optSymbol = this->symbolFormatter.getEnumSymbol(enumDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  auto symbol = optSymbol.value();

  auto [range, fileId] = this->getTokenExpansionRange(enumDecl->getLocation());
  scip::Occurrence occ;
  range.addToOccurrence(occ);
  occ.set_symbol(symbol.data(), symbol.size());
  occ.set_symbol_roles(scip::SymbolRole::Definition);

  auto &doc = this->documentMap[{fileId}];
  doc.occurrences.emplace_back(scip::OccurrenceExt{std::move(occ)});

  llvm::SmallVector<std::string, 4> docComments{};
  this->tryGetDocComment(enumDecl, docComments);
  scip::SymbolInformation symbolInfo;
  for (auto &docComment : docComments) {
    *symbolInfo.add_documentation() = std::move(docComment);
  }
  doc.symbolInfos.emplace(symbol, std::move(symbolInfo));
}

void TuIndexer::saveNamespaceDecl(const clang::NamespaceDecl *namespaceDecl) {
  ENFORCE(namespaceDecl);
  auto optSymbol = this->symbolFormatter.getNamespaceSymbol(namespaceDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  std::string_view symbol = optSymbol.value();

  // getLocation():
  // - for anonymous namespaces, returns the location of the opening brace {
  // - for non-anonymous namespaces, returns the location of the name
  // getBeginLoc():
  // - returns the location of the first keyword
  auto startLoc = [this](auto *n) -> clang::SourceLocation {
    if (n->isAnonymousNamespace()) {
      if (n->isInlineNamespace()) {
        // getBeginLoc() points to 'inline', so find the location of 'namespace'
        auto namespaceToken = clang::Lexer::findNextToken(
            n->getBeginLoc(), this->sourceManager, this->langOptions);
        ENFORCE(namespaceToken.has_value());
        if (namespaceToken.has_value()) {
          return namespaceToken->getLocation();
        }
      }
      return n->getBeginLoc();
    }
    return n->getLocation();
  }(namespaceDecl);

  auto [range, fileId] = this->getTokenExpansionRange(startLoc);

  scip::Occurrence occ;
  range.addToOccurrence(occ);
  occ.set_symbol(symbol.data(), symbol.size());
  occ.set_symbol_roles(scip::SymbolRole::Definition);

  auto &doc = this->documentMap[{fileId}];
  doc.occurrences.emplace_back(scip::OccurrenceExt{std::move(occ)});

  doc.symbolInfos.insert({symbol, scip::SymbolInformation{}});
}

void TuIndexer::emitDocumentOccurrencesAndSymbols(
    bool deterministic, clang::FileID fileId, scip::Document &scipDocument) {
  auto it = this->documentMap.find({fileId});
  if (it == this->documentMap.end()) {
    return;
  }
  auto &doc = it->second;
  for (auto &occExt : doc.occurrences) {
    *scipDocument.add_occurrences() = std::move(occExt.occ);
  }
  extractTransform(
      std::move(doc.symbolInfos), deterministic,
      absl::FunctionRef<void(std::string_view &&, scip::SymbolInformation &&)>(
          [&](auto &&symbolName, auto &&symInfo) {
            symInfo.set_symbol(symbolName.data(), symbolName.size());
            *scipDocument.add_symbols() = std::move(symInfo);
          }));
}

std::pair<FileLocalSourceRange, clang::FileID>
TuIndexer::getTokenExpansionRange(clang::SourceLocation startLoc) const {
  startLoc = this->sourceManager.getExpansionLoc(startLoc);
  auto tokenLength = clang::Lexer::MeasureTokenLength(
      startLoc, this->sourceManager, this->langOptions);
  ENFORCE(tokenLength > 0);
  auto endLoc = startLoc.getLocWithOffset(tokenLength);
  return FileLocalSourceRange::fromNonEmpty(this->sourceManager,
                                            {startLoc, endLoc});
}

void TuIndexer::tryGetDocComment(
    const clang::Decl *decl, llvm::SmallVectorImpl<std::string> &out) const {
  auto &astContext = decl->getASTContext();
  // FIXME(def: hovers, issue:
  // https://github.com/sourcegraph/scip-clang/issues/96)
  if (auto *rawComment = astContext.getRawCommentForAnyRedecl(decl)) {
    auto lines = rawComment->getFormattedLines(this->sourceManager,
                                               astContext.getDiagnostics());
    for (auto &line : lines) {
      out.emplace_back(std::move(line.Text));
    }
  }
}

} // namespace scip_clang
