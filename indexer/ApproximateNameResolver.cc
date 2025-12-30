#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

#include "indexer/ApproximateNameResolver.h"
#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"

#include "spdlog/spdlog.h"

namespace scip_clang {

namespace {
// Reimplementation of the removed CXXRecordDecl::lookupDependentName
llvm::SmallVector<clang::NamedDecl *, 4>
lookupDependentName(clang::CXXRecordDecl *record, clang::DeclarationName name,
                    llvm::function_ref<bool(const clang::NamedDecl *)> filter) {
  llvm::SmallVector<clang::NamedDecl *, 4> results;
  for (auto *decl : record->lookup(name)) {
    if (filter(decl)) {
      results.push_back(decl);
    }
  }
  if (!results.empty())
    return results;

  // Search in base classes
  for (const auto &base : record->bases()) {
    auto *baseType = base.getType()->getAs<clang::RecordType>();
    if (!baseType)
      continue;
    auto *baseRecord =
        llvm::dyn_cast<clang::CXXRecordDecl>(baseType->getDecl());
    if (!baseRecord || !baseRecord->hasDefinition())
      continue;
    auto baseResults =
        lookupDependentName(baseRecord->getDefinition(), name, filter);
    results.append(baseResults.begin(), baseResults.end());
  }
  return results;
}
} // namespace

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
  llvm::SmallPtrSet<const clang::Type *, 2> seen;
  typesToLookup.push_back(type);

  size_t iterations = 0;
  while (!typesToLookup.empty()) {
    iterations++;
    if (iterations > 10'000) {
      spdlog::warn(
          "exceeded 10000 iterations in member lookup for '{}' in type '{}'",
          declNameInfo.getAsString(), clang::QualType(type, 0).getAsString());
      spdlog::info("this is likely a scip-clang bug; please report it at "
                   "https://github.com/sourcegraph/scip-clang/issues");
      break;
    }
    auto *type = typesToLookup.back();
    typesToLookup.pop_back();
    if (seen.find(type) != seen.end()) {
      continue;
    }
    seen.insert(type);
    auto *cxxRecordDecl = Self::tryFindDeclForType(type);
    if (!cxxRecordDecl || !cxxRecordDecl->hasDefinition()) {
      continue;
    }
    cxxRecordDecl = cxxRecordDecl->getDefinition();
    auto lookupResults =
        lookupDependentName(cxxRecordDecl, declNameInfo.getName(), filter);
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