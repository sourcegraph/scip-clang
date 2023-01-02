#ifndef SCIP_CLANG_DEBUG_HELPERS_H
#define SCIP_CLANG_DEBUG_HELPERS_H

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"

namespace scip_clang {
namespace debug {

const char *toString(clang::PPCallbacks::FileChangeReason reason);

llvm::StringRef tryGetPath(const clang::SourceManager &sourceManager,
                           clang::FileID fileId);

} // namespace debug
} // namespace scip_clang

#endif // SCIP_CLANG_DEBUG_HELPERS_H