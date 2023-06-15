#include "spdlog/fmt/fmt.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"

namespace scip_clang {
namespace debug {

const char *toString(clang::PPCallbacks::FileChangeReason reason) {
  using R = clang::PPCallbacks::FileChangeReason;
  switch (reason) {
  case R::EnterFile:
    return "EnterFile";
  case R::ExitFile:
    return "ExitFile";
  case R::RenameFile:
    return "RenameFile";
  case R::SystemHeaderPragma:
    return "SystemHeaderPragma";
  }
}

llvm::StringRef tryGetPath(const clang::SourceManager &sourceManager,
                           clang::FileID fileId) {
  if (fileId.isValid()) {
    if (auto entry = sourceManager.getFileEntryForID(fileId)) {
      auto p = entry->tryGetRealPathName();
      if (!p.empty()) {
        return p;
      }
      return "<empty-path>";
    }
    bool invalid = false;
    auto slocEntry = sourceManager.getSLocEntry(fileId, &invalid);
    if (invalid) {
      return "<null-FileEntry>";
    }
    if (slocEntry.isFile()) {
      return slocEntry.getFile().getName();
    }
    return "<non-file-SlocEntry-for-FileId>";
  }
  return "<invalid-FileID>";
}

std::string formatPtr(const void *ptr) {
  return fmt::format("0x{:16x}", (uint64_t)ptr);
}

/// Slightly tweaked version of SourceLocation::print
std::string formatLoc(const clang::SourceManager &sourceManager,
                      clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return "<invalid loc>";
  }
  if (loc.isFileID()) {
    auto presumedLoc = sourceManager.getPresumedLoc(loc);
    if (presumedLoc.isInvalid()) {
      return "<invalid presumedLoc>";
    }
    return fmt::format("{}:{}:{} (FileID)", presumedLoc.getFilename(),
                       presumedLoc.getLine(), presumedLoc.getColumn());
  }
  auto expansionLoc = sourceManager.getExpansionLoc(loc);
  return fmt::format(
      "{} (MacroID; spellingLoc = {}){}",
      expansionLoc.printToString(sourceManager),
      sourceManager.getSpellingLoc(loc).printToString(sourceManager),
      expansionLoc == loc ? "" : " (note: loc != expansionLoc)");
}

std::string formatRange(const clang::SourceManager &sourceManager,
                        clang::SourceRange range) {
  return formatRange(sourceManager, range.getBegin(), range.getEnd());
}

std::string formatRange(const clang::SourceManager &sourceManager,
                        clang::SourceLocation loc1,
                        clang::SourceLocation loc2) {
  if (loc1.isInvalid() && loc2.isInvalid()) {
    return "<invalid-range>";
  }

  auto formatFileIdRange =
      [&sourceManager](clang::SourceLocation loc1,
                       clang::SourceLocation loc2) -> std::string {
    if (loc1.isFileID() && loc2.isFileID()) {
      if (loc1.isValid() && loc2.isValid()) {
        auto pLoc1 = sourceManager.getPresumedLoc(loc1);
        auto pLoc2 = sourceManager.getPresumedLoc(loc2);
        if (pLoc1.isValid() && pLoc2.isValid()) {
          if (pLoc1.getFilename() == pLoc2.getFilename()) {
            if (pLoc1.getLine() == pLoc2.getLine()) {
              return fmt::format("{}:{}:[{}-{}]", pLoc1.getFilename(),
                                 pLoc1.getLine(), pLoc1.getColumn(),
                                 pLoc2.getColumn());
            }
            return fmt::format("{}:[{}:{}-{}:{}]", pLoc1.getFilename(),
                               pLoc1.getLine(), pLoc1.getColumn(),
                               pLoc2.getLine(), pLoc2.getColumn());
          }
          return fmt::format("[{}:{}:{}]-[{}:{}:{}]", pLoc1.getFilename(),
                             pLoc1.getLine(), pLoc1.getColumn(),
                             pLoc2.getFilename(), pLoc2.getLine(),
                             pLoc2.getColumn());
        } else if (pLoc1.isInvalid() && pLoc2.isInvalid()) {
          return "<invalid-plocs>";
        }
      } else if (loc1.isInvalid() && loc2.isInvalid()) {
        return "<invalid-locs>";
      }
    }
    return "";
  };

  auto rangeStr = formatFileIdRange(loc1, loc2);
  if (!rangeStr.empty()) {
    return fmt::format("{} (FileID)", rangeStr);
  }

  if (loc1.isMacroID() && loc2.isMacroID()) {
    if (loc1.isValid() && loc2.isValid()) {
      auto expLoc1 = sourceManager.getExpansionLoc(loc1);
      auto spLoc1 = sourceManager.getSpellingLoc(loc1);
      auto expLoc2 = sourceManager.getExpansionLoc(loc2);
      auto spLoc2 = sourceManager.getSpellingLoc(loc2);
      auto expansionRangeStr = formatFileIdRange(expLoc1, expLoc2);
      if (!expansionRangeStr.empty()) {
        auto spellingRangeStr = formatFileIdRange(spLoc1, spLoc2);
        if (!spellingRangeStr.empty()) {
          return fmt::format(
              "{} (MacroID; spellingRange = {}){}{}", expansionRangeStr,
              spellingRangeStr,
              expLoc1 == loc1 ? "" : " (note: loc1 != expansionLoc1)",
              expLoc2 == loc2 ? "" : " (note: loc2 != expansionLoc2)");
        }
      }
    }
  }

  // Addressed most common cases, fallback to something that always works.
  return fmt::format("[{}]-[{}]", formatLoc(sourceManager, loc1),
                     formatLoc(sourceManager, loc2));
}

std::string formatDecl(const clang::Decl *decl) {
  if (!decl) {
    return "<null>";
  }
  std::string buf;
  llvm::raw_string_ostream out(buf);
  if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl)) {
    namedDecl->printQualifiedName(out);
  } else {
    decl->print(out);
  }
  return buf;
}

std::string formatKind(clang::NestedNameSpecifier::SpecifierKind kind) {
#define HANDLE_KIND(kind_)                               \
  case clang::NestedNameSpecifier::SpecifierKind::kind_: \
    return #kind_;
  switch (kind) {
    HANDLE_KIND(Identifier)
    HANDLE_KIND(Namespace)
    HANDLE_KIND(NamespaceAlias)
    HANDLE_KIND(TypeSpec)
    HANDLE_KIND(TypeSpecWithTemplate)
    HANDLE_KIND(Global)
    HANDLE_KIND(Super)
  }
#undef HANDLE_KIND
}

std::string formatKind(clang::TemplateName::NameKind kind) {
#define HANDLE_KIND(kind_)                   \
  case clang::TemplateName::NameKind::kind_: \
    return #kind_;
  switch (kind) {
    HANDLE_KIND(Template)
    HANDLE_KIND(OverloadedTemplate)
    HANDLE_KIND(AssumedTemplate)
    HANDLE_KIND(QualifiedTemplate)
    HANDLE_KIND(DependentTemplate)
    HANDLE_KIND(SubstTemplateTemplateParm)
    HANDLE_KIND(SubstTemplateTemplateParmPack)
    HANDLE_KIND(UsingTemplate)
  }
#undef HANDLE_KIND
}

std::string formatKind(clang::TemplateSpecializationKind kind) {
#define HANDLE_KIND(kind_)                             \
  case clang::TemplateSpecializationKind::TSK_##kind_: \
    return #kind_;
  switch (kind) {
    HANDLE_KIND(Undeclared)
    HANDLE_KIND(ImplicitInstantiation)
    HANDLE_KIND(ExplicitSpecialization)
    HANDLE_KIND(ExplicitInstantiationDeclaration)
    HANDLE_KIND(ExplicitInstantiationDefinition)
  }
#undef HANDLE_KIND
}

std::string formatKind(clang::FunctionDecl::TemplatedKind kind) {
#define HANDLE_KIND(kind_)                             \
  case clang::FunctionDecl::TemplatedKind::TK_##kind_: \
    return #kind_;
  switch (kind) {
    HANDLE_KIND(NonTemplate)
    HANDLE_KIND(FunctionTemplate)
    HANDLE_KIND(MemberSpecialization)
    HANDLE_KIND(FunctionTemplateSpecialization)
    HANDLE_KIND(DependentFunctionTemplateSpecialization)
    HANDLE_KIND(DependentNonTemplate)
  }
#undef HANDLE_KIND
}

std::string formatTypeInternals(const clang::QualType &qualType,
                                const clang::ASTContext &astContext) {
  if (qualType.isNull()) {
    return "<null>";
  }
  std::string buf;
  llvm::raw_string_ostream os(buf);
  qualType.dump(os, astContext);
  return buf;
}

} // namespace debug
} // namespace scip_clang
