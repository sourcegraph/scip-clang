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

/// Wrapper around clang's HeuristicResolver for performing best-effort
/// name resolution for unresolved names in template code.
/// Adds caching and handling of UnresolvedUsingValueDecl chains.
class ApproximateNameResolver {
  clang::ASTContext &astContext;
  using Self = ApproximateNameResolver;
  using ResultVec = llvm::SmallVector<const clang::NamedDecl *, 1>;
  absl::flat_hash_map<MemberLookupKey, ResultVec> dependentNameLookupCache;

public:
  ApproximateNameResolver(clang::ASTContext &astContext)
      : astContext(astContext), dependentNameLookupCache() {}

  ResultVec tryResolveMember(const clang::Type *,
                             const clang::DeclarationNameInfo &);

private:
  static clang::CXXRecordDecl *tryFindDeclForType(const clang::Type *);
};

} // namespace scip_clang

#endif // SCIP_CLANG_APPROX_NAME_RESOLVER