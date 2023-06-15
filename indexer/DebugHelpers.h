#ifndef SCIP_CLANG_DEBUG_HELPERS_H
#define SCIP_CLANG_DEBUG_HELPERS_H

#include <string>

#include "clang/AST/Decl.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateName.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class ASTContext;
class Decl;
class QualType;
} // namespace clang

namespace scip_clang {
namespace debug {

const char *toString(clang::PPCallbacks::FileChangeReason reason);

llvm::StringRef tryGetPath(const clang::SourceManager &sourceManager,
                           clang::FileID fileId);

std::string formatPtr(const void *ptr);

std::string formatLoc(const clang::SourceManager &, clang::SourceLocation);

std::string formatRange(const clang::SourceManager &, clang::SourceRange);

std::string formatRange(const clang::SourceManager &,
                        clang::SourceLocation start, clang::SourceLocation end);

std::string formatDecl(const clang::Decl *);

std::string formatKind(clang::NestedNameSpecifier::SpecifierKind);

std::string formatKind(clang::TemplateName::NameKind);

std::string formatKind(clang::TemplateSpecializationKind);

std::string formatKind(clang::FunctionDecl::TemplatedKind);

std::string formatTypeInternals(const clang::QualType &,
                                const clang::ASTContext &);

} // namespace debug
} // namespace scip_clang

#endif // SCIP_CLANG_DEBUG_HELPERS_H
