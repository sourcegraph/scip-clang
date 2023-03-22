#ifndef SCIP_CLANG_DEBUG_HELPERS_H
#define SCIP_CLANG_DEBUG_HELPERS_H

#include <string>

#include "clang/AST/TemplateName.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class Decl;
}

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

std::string formatTemplateNameKind(clang::TemplateName::NameKind);

} // namespace debug
} // namespace scip_clang

#endif // SCIP_CLANG_DEBUG_HELPERS_H