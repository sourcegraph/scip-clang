#include "spdlog/fmt/fmt.h"

#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"

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
    return "<null-FileEntry>";
  }
  return "<invalid-FileID>";
}

std::string formatPtr(const void *ptr) {
  return fmt::format("0x{:16x}", (uint64_t)ptr);
}

std::string formatLocSimple(const clang::SourceManager &sourceManager,
                            clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return "<invalid>";
  }
  auto presumedLoc = sourceManager.getPresumedLoc(loc);
  if (presumedLoc.isInvalid()) {
    return "<invalid-ploc>";
  }
  return fmt::format("{}:{}:{} ({})", presumedLoc.getFilename(),
                     presumedLoc.getLine(), presumedLoc.getColumn(),
                     loc.isFileID() ? "FileID" : "MacroID");
}

std::string formatLoc(const clang::SourceManager &sourceManager,
                      clang::SourceLocation loc) {
  if (loc.isInvalid() || loc.isFileID()) {
    return formatLocSimple(sourceManager, loc);
  }
  auto presumedLoc = sourceManager.getPresumedLoc(loc);
  ENFORCE(presumedLoc.isValid());
  auto spellingLoc = sourceManager.getSpellingLoc(loc);
  auto expansionLoc = sourceManager.getExpansionLoc(loc);
  std::string spellingLocStr = "";
  if (spellingLoc != loc) {
    spellingLocStr = fmt::format(" (spellingLoc = {})",
                                 formatLocSimple(sourceManager, spellingLoc));
  }
  std::string expansionLocStr = "";
  if (expansionLoc != loc) {
    expansionLocStr = fmt::format(" (expansionLoc = {})",
                                  formatLocSimple(sourceManager, expansionLoc));
  }
  return fmt::format("{}:{}:{} {}{}", presumedLoc.getFilename(),
                     presumedLoc.getLine(), presumedLoc.getColumn(),
                     spellingLocStr, expansionLocStr);
}

std::string formatRange(const clang::SourceManager &sourceManager,
                        clang::SourceRange range) {
  return formatRange(sourceManager, range.getBegin(), range.getEnd());
}

std::string formatRange(const clang::SourceManager &sourceManager,
                        clang::SourceLocation loc1,
                        clang::SourceLocation loc2) {
  auto pLoc1 = sourceManager.getPresumedLoc(loc1);
  auto pLoc2 = sourceManager.getPresumedLoc(loc2);
  auto typeStr = (loc1.isFileID() && loc2.isFileID())    ? ("FileID")
                 : (loc1.isFileID() && loc2.isMacroID()) ? ("FileID / MacroID")
                 : (loc1.isMacroID() && loc2.isFileID()) ? ("MacroID / FileID")
                                                         : ("MacroID");
  if (pLoc1.isValid() && pLoc2.isValid()
      && pLoc1.getFileID() == pLoc2.getFileID()) {
    if (pLoc1.getLine() == pLoc2.getLine()) {
      return fmt::format("{}:{}:[{}-{}] (type {})", pLoc1.getFilename(),
                         pLoc1.getLine(), pLoc1.getColumn(), pLoc2.getColumn(),
                         typeStr);
    }
    return fmt::format("{}:[{}:{}-{}:{}] (type {})", pLoc1.getFilename(),
                       pLoc1.getLine(), pLoc1.getColumn(), pLoc2.getLine(),
                       pLoc2.getColumn(), typeStr);
  }
  return fmt::format("[{}]-[{}] (type {})", formatLoc(sourceManager, loc1),
                     formatLoc(sourceManager, loc2), typeStr);
}

} // namespace debug
} // namespace scip_clang