#ifndef SCIP_CLANG_SYMBOL_FORMATTER_H
#define SCIP_CLANG_SYMBOL_FORMATTER_H

#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"

namespace clang {
class Decl;
class DeclContext;
class EnumConstantDecl;
class EnumDecl;
class NamedDecl;
class NamespaceDecl;
class TagDecl;
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
  absl::flat_hash_map<LlvmToAbslHashAdapter<clang::FileID>, uint32_t>
      anonymousTypeCounters;
  std::string scratchBuffer;

public:
  SymbolFormatter(const clang::SourceManager &sourceManager,
                  GetCanonicalPath getCanonicalPath)
      : sourceManager(sourceManager), getCanonicalPath(getCanonicalPath),
        locationBasedCache(), declBasedCache(), scratchBuffer() {}
  SymbolFormatter(const SymbolFormatter &) = delete;
  SymbolFormatter &operator=(const SymbolFormatter &) = delete;

  std::string_view getMacroSymbol(clang::SourceLocation defLoc);

  std::optional<std::string_view>
  getEnumConstantSymbol(const clang::EnumConstantDecl *);

  std::optional<std::string_view> getEnumSymbol(const clang::EnumDecl *);

  std::optional<std::string_view> getNamedDeclSymbol(const clang::NamedDecl *);

  /// Returns nullopt for anonymous namespaces in files for which
  /// getCanonicalPath returns nullopt.
  std::optional<std::string_view>
  getNamespaceSymbol(const clang::NamespaceDecl *);

private:
  std::optional<std::string_view> getContextSymbol(const clang::DeclContext *);
  std::optional<std::string_view> getTagSymbol(const clang::TagDecl *);

  std::optional<std::string_view>
  getSymbolCached(const clang::Decl *,
                  absl::FunctionRef<std::optional<std::string>()>);
};

} // namespace scip_clang

#endif // SCIP_CLANG_SYMBOL_FORMATTER_H