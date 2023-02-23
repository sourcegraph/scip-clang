#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_replace.h"

#include "clang/AST/Decl.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include "indexer/Enforce.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/SymbolFormatter.h"

namespace scip_clang {

std::string_view SymbolFormatter::getMacroSymbol(clang::SourceLocation defLoc) {
  auto it = this->locationBasedCache.find({defLoc});
  if (it != this->locationBasedCache.end()) {
    return std::string_view(it->second);
  }
  // Ignore line directives here because we care about the identity
  // of the macro (based on the containing file), not where it
  // originated from.
  auto defPLoc =
      this->sourceManager.getPresumedLoc(defLoc, /*UseLineDirectives*/ false);
  ENFORCE(defPLoc.isValid());
  std::string_view filename;
  if (auto optRelPath = this->getCanonicalPath(defPLoc.getFileID())) {
    filename = optRelPath->asStringView();
  } else {
    filename = std::string_view(defPLoc.getFilename());
  }
  std::string out{fmt::format("c . todo-pkg todo-version {}:{}:{}#", filename,
                              defPLoc.getLine(), defPLoc.getColumn())};
  auto [newIt, inserted] =
      this->locationBasedCache.insert({{defLoc}, std::move(out)});
  ENFORCE(inserted, "key was missing earlier, so insert should've succeeded");
  return std::string_view(newIt->second);
}

std::optional<std::string_view>
SymbolFormatter::getNamespaceSymbol(const clang::NamespaceDecl *namespaceDecl) {
  ENFORCE(namespaceDecl);
  namespaceDecl = namespaceDecl->getCanonicalDecl();
  auto it = this->declBasedCache.find(namespaceDecl);
  if (it != this->declBasedCache.end()) {
    return std::string_view(it->second);
  }
  if (namespaceDecl->isAnonymousNamespace()) {
    auto mainFileId = this->sourceManager.getMainFileID();
    ENFORCE(mainFileId.isValid());
    auto path = this->getCanonicalPath(mainFileId);
    if (!path.has_value()) {
      // Strictly speaking, this will be suboptimal in the following case:
      // - header 1: in source tree (has canonical path), uses namespace {..}
      // - header 2: in source tree (has canonical path), uses namespace {..}
      // - generated C++ file: in build tree only (no canonical path), and
      //   includes header 1 and 2.
      // We will not emit a symbol that connects the anonymous namespace
      // in header 1 and header 2. However, that is OK, because it is unclear
      // how to handle this case anyways, and anonymous namespaces are
      // rarely (if ever) used in headers.
      return {};
    }
    fmt::format_to(std::back_inserter(this->scratchBuffer), "$ANON/{}\0",
                   path->asStringView());
  } else {
    llvm::raw_string_ostream os(this->scratchBuffer);
    namespaceDecl->printQualifiedName(os);
    absl::StrReplaceAll({{"::", "/"}}, &this->scratchBuffer);
  }
  std::string out{
      fmt::format("c . todo-pkg todo-version {}/", this->scratchBuffer)};
  this->scratchBuffer.clear();
  auto [newIt, inserted] =
      this->declBasedCache.insert({namespaceDecl, std::move(out)});
  ENFORCE(inserted);
  return std::string_view(newIt->second);
}

} // namespace scip_clang
