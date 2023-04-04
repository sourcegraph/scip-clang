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
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TypeLoc.h"
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
#include "indexer/SymbolFormatter.h"

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
  std::string message;
  ENFORCE(
      [&]() -> bool {
        absl::flat_hash_set<std::string_view> paths{};
        message = "non-file based macros found in:\n";
        for (auto &macro : this->nonFileBasedMacros) {
          auto fileId =
              this->sourceManager->getFileID(macro.defInfo->getDefinitionLoc());
          auto path = llvm_ext::toStringView(
              debug::tryGetPath(*this->sourceManager, fileId));
          bool inserted = paths.insert(path).second;
          if (inserted) {
            message.append(fmt::format("  {}\n", path));
          }
        }
        return paths.size() <= 1;
      }(),
      "{}", message);
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
                     const clang::ASTContext &astContext,
                     SymbolFormatter &symbolFormatter,
                     GetStableFileId getStableFileId)
    : sourceManager(sourceManager), langOptions(langOptions),
      astContext(astContext), symbolFormatter(symbolFormatter), documentMap(),
      getStableFileId(getStableFileId), externalSymbols(),
      forwardDeclarations() {}

void TuIndexer::saveBindingDecl(const clang::BindingDecl &bindingDecl) {
  auto optSymbol = this->symbolFormatter.getBindingSymbol(bindingDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  this->saveDefinition(optSymbol.value(), bindingDecl.getLocation(),
                       std::nullopt);
}

void TuIndexer::saveEnumConstantDecl(
    const clang::EnumConstantDecl &enumConstantDecl) {
  auto optSymbol =
      this->symbolFormatter.getEnumConstantSymbol(enumConstantDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  auto symbol = optSymbol.value();

  scip::SymbolInformation symbolInfo;
  for (auto &docComment : this->tryGetDocComment(enumConstantDecl).lines) {
    *symbolInfo.add_documentation() = std::move(docComment);
  }

  ENFORCE(enumConstantDecl.getBeginLoc() == enumConstantDecl.getLocation());
  this->saveDefinition(symbol, enumConstantDecl.getLocation(),
                       std::move(symbolInfo));
}

void TuIndexer::saveEnumDecl(const clang::EnumDecl &enumDecl) {
  this->saveTagDecl(enumDecl);
}

void TuIndexer::saveEnumTypeLoc(const clang::EnumTypeLoc &enumTypeLoc) {
  this->saveTagTypeLoc(enumTypeLoc);
}

void TuIndexer::saveFieldDecl(const clang::FieldDecl &fieldDecl) {
  auto optSymbol = this->symbolFormatter.getFieldSymbol(fieldDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  scip::SymbolInformation symbolInfo{};
  for (auto &docComment : this->tryGetDocComment(fieldDecl).lines) {
    *symbolInfo.add_documentation() = std::move(docComment);
  }
  this->saveDefinition(optSymbol.value(), fieldDecl.getLocation(), symbolInfo);
}

void TuIndexer::saveFieldReference(const clang::FieldDecl &fieldDecl,
                                   clang::SourceLocation loc) {
  if (auto optSymbol = this->symbolFormatter.getFieldSymbol(fieldDecl)) {
    this->saveReference(*optSymbol, loc);
  }
}

void TuIndexer::saveFunctionDecl(const clang::FunctionDecl &functionDecl) {
  auto optSymbol = this->symbolFormatter.getFunctionSymbol(functionDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  std::string_view symbol = optSymbol.value();

  if (functionDecl.isPure() || functionDecl.isThisDeclarationADefinition()) {
    scip::SymbolInformation symbolInfo{};
    for (auto &docComment : this->tryGetDocComment(functionDecl).lines) {
      *symbolInfo.add_documentation() = std::move(docComment);
    }
    if (auto *cxxMethodDecl =
            llvm::dyn_cast<clang::CXXMethodDecl>(&functionDecl)) {
      for (auto &overridenMethodDecl : cxxMethodDecl->overridden_methods()) {
        if (auto optOverridenSymbol =
                this->symbolFormatter.getFunctionSymbol(*overridenMethodDecl)) {
          scip::Relationship rel{};
          rel.set_symbol(optOverridenSymbol->data(),
                         optOverridenSymbol->size());
          rel.set_is_implementation(true);
          rel.set_is_reference(true);
          *symbolInfo.add_relationships() = std::move(rel);
        }
      }
    }
    // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/123)
    // Kythe uses DeclarationNameInfo::getSourceRange(), which seems like
    // the right API for getting the right range for multi-token names like
    // 'operator<<' etc., but when I tried using that here, it returned
    // incorrect results for every kind of name.
    //
    // Specifically, for simple identifiers, it would return a blank range,
    // and for 'operator<<', it would exclude the range of '<<'.
    // So just rely on the single token implementation for now.
    this->saveDefinition(symbol, functionDecl.getLocation(),
                         std::move(symbolInfo));
  } else {
    this->saveForwardDeclaration(symbol, functionDecl.getLocation(),
                                 this->tryGetDocComment(functionDecl));
  }
}

void TuIndexer::saveNamespaceDecl(const clang::NamespaceDecl &namespaceDecl) {
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
  auto startLoc = [this, &namespaceDecl]() -> clang::SourceLocation {
    if (namespaceDecl.isAnonymousNamespace()) {
      if (namespaceDecl.isInlineNamespace()) {
        // getBeginLoc() points to 'inline', so find the location of 'namespace'
        auto namespaceToken =
            clang::Lexer::findNextToken(namespaceDecl.getBeginLoc(),
                                        this->sourceManager, this->langOptions);
        ENFORCE(namespaceToken.has_value());
        if (namespaceToken.has_value()) {
          return namespaceToken->getLocation();
        }
      }
      return namespaceDecl.getBeginLoc();
    }
    return namespaceDecl.getLocation();
  }();

  // The blank SymbolInformation looks a little weird, but we
  // don't need to set the symbol name since that's handled by
  // saveDefinition, and we generally don't want to try to infer
  // doc comments for namespaces since preceding comments are
  // likely to be free-floating top-level comments.
  this->saveDefinition(symbol, startLoc, scip::SymbolInformation{});
}

void TuIndexer::saveNestedNameSpecifierLoc(
    const clang::NestedNameSpecifierLoc &argNameSpecLoc) {
  clang::NestedNameSpecifierLoc nameSpecLoc = argNameSpecLoc;

  auto tryEmit = [this](clang::NestedNameSpecifierLoc nameSpecLoc,
                        const clang::NamedDecl &namedDecl) {
    if (auto optSymbol = this->symbolFormatter.getNamedDeclSymbol(namedDecl)) {
      // Don't use nameSpecLoc.getLocalSourceRange() as that may give
      // two MacroID SourceLocations, in case the NestedNameSpecifier
      // arises from a macro expansion.
      this->saveReference(*optSymbol, nameSpecLoc.getLocalBeginLoc());
    }
  };

  while (nameSpecLoc.hasQualifier()) {
    auto nameSpec = nameSpecLoc.getNestedNameSpecifier();
    using Kind = clang::NestedNameSpecifier;
    switch (nameSpec->getKind()) {
    case Kind::Namespace: {
      tryEmit(nameSpecLoc, *nameSpec->getAsNamespace());
      break;
    }
    case Kind::TypeSpec: {
      auto *type = nameSpec->getAsType();
      if (auto *tagDecl = type->getAsTagDecl()) {
        tryEmit(nameSpecLoc, *tagDecl);
      }
      break;
    }
    // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/109)
    // Handle all these other cases too
    case Kind::Identifier:
    case Kind::NamespaceAlias:
    case Kind::Global:
    case Kind::Super:
    case Kind::TypeSpecWithTemplate:
      // NOTE: Adding support for TypeSpecWithTemplate needs extra care
      // for (partial) template specializations. Example code:
      //
      //   template <typename T0>
      //   struct X {
      //     template <typename T1>
      //     struct Y {};
      //   };
      //
      //   template <>
      //   struct X {
      //     template <typename A>
      //     struct Y { int[42] magic; };
      //   };
      //
      //   template <typename U0> void f() {
      //     typename X<U0>::template Y<U0> y{};
      //                   //^^^^^^^^^^^^^^ TypeSpecWithTemplate
      //     std::cout << sizeof(y) << '\n';
      //   }
      //
      // In 'template Y<U0>', clangd will navigate to 'Y' in the body of 'X',
      // even when there is partial template specialization of X
      // (so calling f<int>() will print a different value).
      // Ideally, we should surface such specializations too.
      break;
    }
    nameSpecLoc = nameSpecLoc.getPrefix();
  }
}

void TuIndexer::saveRecordDecl(const clang::RecordDecl &recordDecl) {
  this->saveTagDecl(recordDecl);
  // Superclass declarations will be visited during Visit*TypeLoc methods.
}

void TuIndexer::saveRecordTypeLoc(const clang::RecordTypeLoc &recordTypeLoc) {
  this->saveTagTypeLoc(recordTypeLoc);
}

void TuIndexer::saveTagDecl(const clang::TagDecl &tagDecl) {
  auto optSymbol = this->symbolFormatter.getTagSymbol(tagDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  auto symbol = optSymbol.value();

  if (!tagDecl.isThisDeclarationADefinition()) {
    this->saveForwardDeclaration(symbol, tagDecl.getLocation(),
                                 this->tryGetDocComment(tagDecl));
    return;
  }

  scip::SymbolInformation symbolInfo;
  for (auto &docComment : this->tryGetDocComment(tagDecl).lines) {
    *symbolInfo.add_documentation() = std::move(docComment);
  }

  if (auto *cxxRecordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(&tagDecl)) {
    for (const clang::CXXBaseSpecifier &cxxBaseSpecifier :
         cxxRecordDecl->bases()) {
      if (auto *tagDecl = cxxBaseSpecifier.getType()->getAsTagDecl()) {
        auto optRelatedSymbol = this->symbolFormatter.getTagSymbol(*tagDecl);
        if (!optRelatedSymbol.has_value()) {
          continue;
        }
        scip::Relationship rel{};
        rel.set_symbol(optRelatedSymbol->data(), optRelatedSymbol->size());
        rel.set_is_implementation(true);
        *symbolInfo.add_relationships() = std::move(rel);
      }
    }
  }

  this->saveDefinition(symbol, tagDecl.getLocation(), std::move(symbolInfo));
}

void TuIndexer::saveTagTypeLoc(const clang::TagTypeLoc &tagTypeLoc) {
  if (tagTypeLoc.isDefinition()) {
    return;
  }
  if (auto optSymbol =
          this->symbolFormatter.getTagSymbol(*tagTypeLoc.getDecl())) {
    this->saveReference(optSymbol.value(), tagTypeLoc.getNameLoc());
  }
}

#define SAVE_TEMPLATE_PARM(name_)                                          \
  void TuIndexer::save##name_##Decl(const clang::name_##Decl &decl) {      \
    if (auto optSymbol = this->symbolFormatter.get##name_##Symbol(decl)) { \
      this->saveDefinition(*optSymbol, decl.getLocation(), std::nullopt);  \
    }                                                                      \
  }
FOR_EACH_TEMPLATE_PARM_TO_BE_INDEXED(SAVE_TEMPLATE_PARM)
#undef SAVE_TEMPLATE_PARM

void TuIndexer::saveTemplateTypeParmTypeLoc(
    const clang::TemplateTypeParmTypeLoc &templateTypeParmTypeLoc) {
  if (auto optSymbol = this->symbolFormatter.getTemplateTypeParmSymbol(
          *templateTypeParmTypeLoc.getDecl())) {
    this->saveReference(*optSymbol, templateTypeParmTypeLoc.getNameLoc());
  }
}

void TuIndexer::saveTemplateSpecializationTypeLoc(
    const clang::TemplateSpecializationTypeLoc &templateSpecializationTypeLoc) {
  auto *templateSpecializationType = templateSpecializationTypeLoc.getTypePtr();
  auto templateName = templateSpecializationType->getTemplateName();
  using Kind = clang::TemplateName::NameKind;
  switch (templateName.getKind()) {
  case Kind::Template: {
    auto *templateDecl = templateName.getAsTemplateDecl();
    std::optional<std::string_view> optSymbol;
    if (auto *classTemplateDecl =
            llvm::dyn_cast<clang::ClassTemplateDecl>(templateDecl)) {
      optSymbol = this->symbolFormatter.getRecordSymbol(
          *classTemplateDecl->getTemplatedDecl());
    } else if (auto *typeAliasTemplateDecl =
                   llvm::dyn_cast<clang::TypeAliasTemplateDecl>(templateDecl)) {
      optSymbol = this->symbolFormatter.getTypedefNameSymbol(
          *typeAliasTemplateDecl->getTemplatedDecl());
    } else if (auto *templateTemplateParmDecl =
                   llvm::dyn_cast<clang::TemplateTemplateParmDecl>(
                       templateDecl)) {
      optSymbol = this->symbolFormatter.getTemplateTemplateParmSymbol(
          *templateTemplateParmDecl);
    }
    if (optSymbol.has_value()) {
      this->saveReference(*optSymbol,
                          templateSpecializationTypeLoc.getTemplateNameLoc());
    }
    break;
  }
  case Kind::OverloadedTemplate:
  case Kind::AssumedTemplate:
  case Kind::QualifiedTemplate:
  case Kind::DependentTemplate:
  case Kind::SubstTemplateTemplateParm:
  case Kind::SubstTemplateTemplateParmPack:
  case Kind::UsingTemplate:
    break;
  }
}

void TuIndexer::saveTypedefNameDecl(
    const clang::TypedefNameDecl &typedefNameDecl) {
  auto optSymbol = this->symbolFormatter.getNamedDeclSymbol(typedefNameDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  scip::SymbolInformation symbolInfo{};
  for (auto &docComment : this->tryGetDocComment(typedefNameDecl).lines) {
    *symbolInfo.add_documentation() = std::move(docComment);
  }
  this->saveDefinition(*optSymbol, typedefNameDecl.getLocation(),
                       std::move(symbolInfo));
}

void TuIndexer::saveVarDecl(const clang::VarDecl &varDecl) {
  if (llvm::isa<clang::DecompositionDecl>(&varDecl)) {
    // Individual bindings will be visited by VisitBindingDecl
    return;
  }
  if (varDecl.isLocalVarDeclOrParm()) {
    auto optSymbol = this->symbolFormatter.getLocalVarOrParmSymbol(varDecl);
    if (!optSymbol.has_value()) {
      return;
    }
    this->saveDefinition(optSymbol.value(), varDecl.getLocation(),
                         std::nullopt);
  }
  if (varDecl.isStaticDataMember() || varDecl.isFileVarDecl()) {
    // Non-static data members are handled by saveFieldDecl
    auto optSymbol = this->symbolFormatter.getVarSymbol(varDecl);
    if (!optSymbol.has_value()) {
      return;
    }
    scip::SymbolInformation symbolInfo{};
    for (auto &docComment : this->tryGetDocComment(varDecl).lines) {
      *symbolInfo.add_documentation() = std::move(docComment);
    }
    this->saveDefinition(optSymbol.value(), varDecl.getLocation(), symbolInfo);
  }
}

void TuIndexer::saveCXXConstructExpr(
    const clang::CXXConstructExpr &cxxConstructExpr) {
  if (auto *cxxConstructorDecl = cxxConstructExpr.getConstructor()) {
    if (auto optSymbol =
            this->symbolFormatter.getFunctionSymbol(*cxxConstructorDecl)) {
      this->saveReference(*optSymbol, cxxConstructExpr.getBeginLoc());
    }
  }
}

void TuIndexer::saveDeclRefExpr(const clang::DeclRefExpr &declRefExpr) {
  // In the presence of 'using', prefer going to the 'using' instead
  // of directly dereferencing.
  auto *foundDecl = declRefExpr.getFoundDecl();
  if (!foundDecl) {
    return;
  }
  auto optSymbol = this->symbolFormatter.getNamedDeclSymbol(*foundDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  // A::B::C
  //       ^ getLocation()
  // ^^^^^^ getSourceRange()
  // ^ getExprLoc()
  this->saveReference(optSymbol.value(), declRefExpr.getLocation());
  // ^ TODO: Add read-write access to the symbol role here
}

void TuIndexer::saveMemberExpr(const clang::MemberExpr &memberExpr) {
  auto *namedDecl =
      llvm::dyn_cast<clang::NamedDecl>(memberExpr.getMemberDecl());
  if (!namedDecl) {
    return;
  }
  auto optSymbol = this->symbolFormatter.getNamedDeclSymbol(*namedDecl);
  if (!optSymbol.has_value()) {
    return;
  }
  // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/123)
  // The ideal API here is probably getMemberNameInfo().getSourceRange(),
  // similar to the operator case, but that doesn't give the expected result.
  if (!memberExpr.getMemberNameInfo().getName().isIdentifier()) {
    return;
  }
  this->saveReference(optSymbol.value(), memberExpr.getMemberLoc());
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

void TuIndexer::emitExternalSymbols(bool deterministic,
                                    scip::Index &indexShard) {
  scip_clang::extractTransform(
      std::move(this->externalSymbols), deterministic,
      absl::FunctionRef<void(std::string_view &&, scip::SymbolInformation &&)>(
          [&](auto &&symbol, auto &&symbolInfo) {
            symbolInfo.set_symbol(symbol.data(), symbol.size());
            *indexShard.add_external_symbols() = std::move(symbolInfo);
          }));
}

void TuIndexer::emitForwardDeclarations(bool deterministic,
                                        scip::Index &forwardDeclIndex) {
  scip_clang::extractTransform(
      std::move(this->forwardDeclarations), deterministic,
      absl::FunctionRef<void(std::string_view &&, DocComment &&)>(
          [&](auto &&symbol, auto &&docComment) {
            scip::SymbolInformation symbolInfo{};
            for (auto &line : docComment.lines) {
              *symbolInfo.add_documentation() = std::move(line);
            }
            symbolInfo.set_symbol(symbol.data(), symbol.size());
            // Add a forward declaration SymbolRole here
            // once https://github.com/sourcegraph/scip/issues/131 is fixed.
            *forwardDeclIndex.add_external_symbols() = std::move(symbolInfo);
          }));
}

std::pair<FileLocalSourceRange, clang::FileID>
TuIndexer::getTokenExpansionRange(
    clang::SourceLocation startExpansionLoc) const {
  auto tokenLength = clang::Lexer::MeasureTokenLength(
      startExpansionLoc, this->sourceManager, this->langOptions);
  ENFORCE(tokenLength > 0);
  auto endLoc = startExpansionLoc.getLocWithOffset(tokenLength);
  return FileLocalSourceRange::fromNonEmpty(this->sourceManager,
                                            {startExpansionLoc, endLoc});
}

void TuIndexer::saveForwardDeclaration(std::string_view symbol,
                                       clang::SourceLocation loc,
                                       DocComment &&docComments) {
  this->saveReference(symbol, loc);
  auto [it, inserted] = this->forwardDeclarations.emplace(symbol, docComments);
  if (!inserted && it->second.lines.empty() && !docComments.lines.empty()) {
    it->second.lines = std::move(docComments.lines);
  }
  return;
}

void TuIndexer::saveReference(std::string_view symbol,
                              clang::SourceLocation loc, int32_t extraRoles) {
  auto expansionLoc = this->sourceManager.getExpansionLoc(loc);
  auto fileId = this->sourceManager.getFileID(expansionLoc);
  auto optStableFileId = this->getStableFileId(fileId);
  if (!optStableFileId.has_value() || !optStableFileId->isInProject) {
    return;
  }
  ENFORCE((extraRoles & scip::SymbolRole::Definition) == 0,
          "use saveDefinition instead");
  (void)this->saveOccurrence(symbol, expansionLoc, extraRoles);
}

void TuIndexer::saveDefinition(
    std::string_view symbol, clang::SourceLocation loc,
    std::optional<scip::SymbolInformation> &&optSymbolInfo,
    int32_t extraRoles) {
  auto expansionLoc = this->sourceManager.getExpansionLoc(loc);
  auto fileId = this->sourceManager.getFileID(expansionLoc);
  auto optStableFileId = this->getStableFileId(fileId);
  if (!optStableFileId.has_value()) {
    return;
  }
  if (optStableFileId->isInProject) {
    auto &doc = this->saveOccurrence(symbol, expansionLoc,
                                     extraRoles | scip::SymbolRole::Definition);
    if (optSymbolInfo.has_value()) {
      doc.symbolInfos.emplace(symbol, std::move(optSymbolInfo.value()));
    }
  } else if (optSymbolInfo.has_value()) {
    this->saveExternalSymbol(symbol, std::move(*optSymbolInfo));
  }
}

void TuIndexer::saveExternalSymbol(std::string_view symbol,
                                   scip::SymbolInformation &&symbolInfo) {
  auto [it, inserted] =
      this->externalSymbols.emplace(symbol, std::move(symbolInfo));
  if (!inserted && it->second.documentation().empty()
      && !symbolInfo.documentation().empty()) {
    *it->second.mutable_documentation() =
        std::move(*symbolInfo.mutable_documentation());
  }
}

PartialDocument &TuIndexer::saveOccurrence(std::string_view symbol,
                                           clang::SourceLocation expansionLoc,
                                           int32_t allRoles) {
  auto [range, fileId] = this->getTokenExpansionRange(expansionLoc);
  scip::Occurrence occ;
  range.addToOccurrence(occ);
  occ.set_symbol(symbol.data(), symbol.size());
  occ.set_symbol_roles(allRoles);
  auto &doc = this->documentMap[{fileId}];
  doc.occurrences.emplace_back(scip::OccurrenceExt{std::move(occ)});
  return doc;
}

} // namespace scip_clang

// This is buggy even in clangd, so roll our own workaround.
// https://github.com/sourcegraph/scip-clang/issues/105#issuecomment-1451252984
static bool
checkIfCommentBelongsToPreviousEnumCase(const clang::Decl &decl,
                                        const clang::RawComment &comment) {
  if (auto *enumConstantDecl = llvm::dyn_cast<clang::EnumConstantDecl>(&decl)) {
    if (auto *enumDecl = llvm::dyn_cast<clang::EnumDecl>(
            enumConstantDecl->getDeclContext())) {
      int i = -1;
      const clang::EnumConstantDecl *previous = nullptr;
      for (const clang::EnumConstantDecl *current : enumDecl->enumerators()) {
        i++;
        if (i == 64) {
          // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/105):
          // There doesn't seem to be any API for quickly getting the
          // previous EnumConstantDecl given an existing one. Attempting to
          // find decls by iterating through the EnumDecl will be worst-case
          // quadratic, so add a somewhat arbitrary limit here.
          // We could potentially build up a cache for O(N) runtime,
          // but it seems a bit overkill for now.
          return false;
        }
        if (current != enumConstantDecl) {
          previous = current;
          continue;
        }
        return previous && (previous->getBeginLoc() > comment.getBeginLoc());
      }
    }
  }
  return false;
}

namespace scip_clang {

DocComment TuIndexer::tryGetDocComment(const clang::Decl &decl) const {
  auto &astContext = decl.getASTContext();
  // FIXME(def: hovers, issue:
  // https://github.com/sourcegraph/scip-clang/issues/96)
  if (auto *rawComment = astContext.getRawCommentForAnyRedecl(&decl)) {
    if (::checkIfCommentBelongsToPreviousEnumCase(decl, *rawComment)) {
      return {};
    }
    DocComment out{};
    for (auto &line : rawComment->getFormattedLines(
             this->sourceManager, astContext.getDiagnostics())) {
      out.lines.emplace_back(std::move(line.Text));
    }
    return out;
  }
  return {};
}

} // namespace scip_clang
