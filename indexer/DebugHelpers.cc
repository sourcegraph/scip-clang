#include "spdlog/fmt/fmt.h"

#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"

#include "indexer/DebugHelpers.h"

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
    return "<null-FileEntry>";
  }
  return "<invalid-FileID>";
}

std::string formatPtr(const void *ptr) {
  return fmt::format("0x{:16x}", (uint64_t)ptr);
}

} // namespace debug
} // namespace scip_clang