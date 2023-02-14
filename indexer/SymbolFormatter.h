#ifndef SCIP_CLANG_SYMBOL_FORMATTER_H
#define SCIP_CLANG_SYMBOL_FORMATTER_H

#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include "indexer/LLVMAdapter.h"
#include "indexer/Path.h"

namespace clang {
class Decl;
class NamespaceDecl;
} // namespace clang

namespace scip_clang {

using GetCanonicalPath =
    absl::FunctionRef<std::optional<RootRelativePathRef>(clang::FileID)>;

class SymbolFormatter final {
  const clang::SourceManager &sourceManager;
  GetCanonicalPath getCanonicalPath;

  // Q: Should we allocate into an arena instead of having standalone
  // std::string values here?

  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::SourceLocation>, std::string>
      locationBasedCache;
  absl::flat_hash_map<const clang::Decl *, std::string> declBasedCache;
  std::string scratchBuffer;

public:
  SymbolFormatter(const clang::SourceManager &sourceManager,
                  GetCanonicalPath getCanonicalPath)
      : sourceManager(sourceManager), getCanonicalPath(getCanonicalPath),
        locationBasedCache(), declBasedCache(), scratchBuffer() {}
  SymbolFormatter(const SymbolFormatter &) = delete;
  SymbolFormatter &operator=(const SymbolFormatter &) = delete;

  std::string_view getMacroSymbol(clang::SourceLocation defLoc);

  /// Returns nullopt for anonymous namespaces in files for which
  /// getCanonicalPath returns nullopt.
  std::optional<std::string_view>
  getNamespaceSymbol(const clang::NamespaceDecl *);
};

} // namespace scip_clang

#endif // SCIP_CLANG_SYMBOL_FORMATTER_H