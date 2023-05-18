#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/SmallVector.h"

#include "indexer/ApproximateNameResolver.h"
#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"

#include "spdlog/spdlog.h"

namespace scip_clang {

MemberLookupKey::MemberLookupKey(const clang::Type *type,
                                 const clang::DeclarationNameInfo &declNameInfo)
    : canonicalTypePtr(type->getCanonicalTypeInternal().getTypePtrOrNull()),
      declarationName(declNameInfo.getName().getAsOpaqueInteger()) {
  ENFORCE(this->canonicalTypePtr,
          "member lookups should exit early for null base type");
}

llvm::SmallVector<const clang::NamedDecl *, 1>
ApproximateNameResolver::tryResolveMember(
    const clang::Type *type, const clang::DeclarationNameInfo &declNameInfo) {
  llvm::SmallVector<const clang::NamedDecl *, 1> results;
  if (!type) {
    return results;
  }
  MemberLookupKey key{type, declNameInfo};
  auto it = this->dependentNameLookupCache.find(key);
  if (it != this->dependentNameLookupCache.end()) {
    return it->second;
  }

  auto filter = [](const clang::NamedDecl *namedDecl) -> bool {
    return !llvm::isa<clang::UsingDecl>(namedDecl);
  };

  llvm::SmallVector<const clang::Type *, 2> typesToLookup;
  typesToLookup.push_back(type);

  while (!typesToLookup.empty()) {
    auto *type = typesToLookup.back();
    typesToLookup.pop_back();
    auto *cxxRecordDecl = Self::tryFindDeclForType(type);
    if (!cxxRecordDecl || !cxxRecordDecl->hasDefinition()) {
      continue;
    }
    cxxRecordDecl = cxxRecordDecl->getDefinition();
    auto lookupResults =
        cxxRecordDecl->lookupDependentName(declNameInfo.getName(), filter);
    for (auto *namedDecl : lookupResults) {
      auto *unresolvedUsingValueDecl =
          llvm::dyn_cast<clang::UnresolvedUsingValueDecl>(namedDecl);
      if (!unresolvedUsingValueDecl) {
        results.push_back(namedDecl);
        continue;
      }
      if (auto *nestedNameSpecifier =
              unresolvedUsingValueDecl->getQualifier()) {
        if (auto *innerType = nestedNameSpecifier->getAsType()) {
          typesToLookup.push_back(innerType);
        }
      }
    }
  }

  this->dependentNameLookupCache.insert({key, results});
  return results;
}

// static
clang::CXXRecordDecl *
ApproximateNameResolver::tryFindDeclForType(const clang::Type *type) {
  if (const auto *recordType = type->getAs<clang::RecordType>())
    return llvm::dyn_cast<clang::CXXRecordDecl>(recordType->getDecl());

  if (const auto *injectedClassNameType =
          type->getAs<clang::InjectedClassNameType>())
    type = injectedClassNameType->getInjectedSpecializationType()
               .getTypePtrOrNull();
  if (!type)
    return nullptr;

  const auto *templateSpecializationType =
      type->getAs<clang::TemplateSpecializationType>();
  if (!templateSpecializationType)
    return nullptr;

  auto *templateDecl = dyn_cast_or_null<clang::ClassTemplateDecl>(
      templateSpecializationType->getTemplateName().getAsTemplateDecl());
  if (!templateDecl)
    return nullptr;

  return templateDecl->getTemplatedDecl();
}

} // namespace scip_clang