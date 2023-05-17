#ifndef SCIP_CLANG_APPROX_NAME_RESOLVER
#define SCIP_CLANG_APPROX_NAME_RESOLVER

#include <utility>

#include "absl/container/flat_hash_map.h"

#include "llvm/ADT/SmallVector.h"

#include "indexer/Derive.h"

namespace clang {
class ASTContext;
class CXXRecordDecl;
class DeclarationNameInfo;
class NamedDecl;
class QualType;
class Type;
} // namespace clang

namespace scip_clang {

class MemberLookupKey {
  const clang::Type *canonicalTypePtr;
  uintptr_t declarationName;

public:
  MemberLookupKey(const clang::Type *, const clang::DeclarationNameInfo &);

  template <typename H>
  friend H AbslHashValue(H h, const MemberLookupKey &self) {
    return H::combine(std::move(h), (void *)self.canonicalTypePtr,
                      self.declarationName);
  }

  DERIVE_CMP_ALL(MemberLookupKey)
};

/// Type similar to clangd's HeuristicResolver, used for performing
/// best-effort name resolution when encountered unresolved names.
///
/// We don't directly reuse the code from clangd, since it's not that
/// much code (<300 lines), and having own code let's us evolve it
/// independently and add different heuristics.
///
/// (Named differently to reduce risk of confusion.)
class ApproximateNameResolver {
  [[maybe_unused]] const clang::ASTContext &astContext; // for debugging
  using Self = ApproximateNameResolver;
  using ResultVec = llvm::SmallVector<const clang::NamedDecl *, 1>;
  absl::flat_hash_map<MemberLookupKey, ResultVec> dependentNameLookupCache;

public:
  ApproximateNameResolver(const clang::ASTContext &astContext)
      : astContext(astContext), dependentNameLookupCache() {}

  ResultVec tryResolveMember(const clang::Type *,
                             const clang::DeclarationNameInfo &);

private:
  // Analog to HeuristicResolver.cc's resolveTypeToRecordDecl
  static clang::CXXRecordDecl *tryFindDeclForType(const clang::Type *);
};

} // namespace scip_clang

#endif // SCIP_CLANG_APPROX_NAME_RESOLVER