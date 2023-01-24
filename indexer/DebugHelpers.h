#ifndef SCIP_CLANG_DEBUG_HELPERS_H
#define SCIP_CLANG_DEBUG_HELPERS_H

#include <string>

#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"

namespace scip_clang {
namespace debug {

const char *toString(clang::PPCallbacks::FileChangeReason reason);

llvm::StringRef tryGetPath(const clang::SourceManager &sourceManager,
                           clang::FileID fileId);

std::string formatPtr(const void *ptr);

} // namespace debug
} // namespace scip_clang

#endif // SCIP_CLANG_DEBUG_HELPERS_H