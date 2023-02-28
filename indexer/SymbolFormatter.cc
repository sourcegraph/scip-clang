#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_replace.h"

#include "clang/AST/Decl.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include "indexer/Enforce.h"
#include "indexer/Hash.h"
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

std::string_view
SymbolFormatter::getSymbolCached(const clang::Decl *decl,
                                 absl::FunctionRef<std::string()> getSymbol) {
  ENFORCE(decl);
  // NOTE(def: canonical-decl): Improve cache hit ratio by using
  // the canonical decl as the key.
  //
  // It is a little subtle as to why picking the canonical decl will
  // give correct results. In particular, the canonical decl may
  // change depending on include order. For example, if you have:
  //   void f(int x);     // In A.h
  //   void f(int x = 0); // In B.h
  // Then depending on #include order of A.h and B.h, the canonical decl
  // will be different, since it is most commonly determined based on which
  // one appears first.
  //
  // However, this doesn't matter because the symbol name of the function
  // can only depend on:
  // - The path of the main TU (if there is an anonymous namespace
  //   in the decl context chain)
  // - Names of namespaces.
  // It cannot depend on the path of the header itself (header paths affect
  // symbol names for anonymous types, but methods in different anonymous
  // type declarations would never be mapped to the same symbol anyways),
  // so even if the exact canonical decl is different, the symbol name
  // will be the same exactly when required.
  decl = decl->getCanonicalDecl();
  auto it = this->declBasedCache.find(decl);
  if (it != this->declBasedCache.end()) {
    return std::string_view(it->second);
  }
  auto symbol = getSymbol();
  if (symbol.empty()) {
    return "";
  }
  auto [newIt, inserted] =
      this->declBasedCache.insert({decl, std::move(symbol)});
  ENFORCE(inserted);
  return std::string_view(newIt->second);
}

std::optional<std::string_view>
SymbolFormatter::getContextSymbol(const clang::DeclContext *declContext) {
  if (auto namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) {
    return this->getNamespaceSymbol(namespaceDecl).value_or("");
  }
  if (auto tagDecl = llvm::dyn_cast<clang::TagDecl>(declContext)) {
    return this->getTagSymbol(tagDecl);
  }
  if (llvm::isa<clang::TranslationUnitDecl>(declContext)
      || llvm::isa<clang::ExternCContextDecl>(declContext)) {
    auto decl = llvm::dyn_cast<clang::Decl>(declContext);
    return this->getSymbolCached(
        decl, [&]() -> std::string { return "c . todo-pkg todo-version "; });
  }
  // TODO: Handle all cases of DeclContext here:
  // Done
  // - TranslationUnitDecl
  // - ExternCContext
  // - NamespaceDecl
  // - TagDecl
  // Pending:
  // - OMPDeclareReductionDecl
  // - OMPDeclareMapperDecl
  // - FunctionDecl
  // - ObjCMethodDecl
  // - ObjCContainerDecl
  // - LinkageSpecDecl
  // - ExportDecl
  // - BlockDecl
  // - CapturedDecl
  return std::nullopt;
}

std::optional<std::string_view>
SymbolFormatter::getTagSymbol(const clang::TagDecl *tagDecl) {
  return this->getSymbolCached(tagDecl, [&]() -> std::string {
    auto optContextSymbol = this->getContextSymbol(tagDecl->getDeclContext());
    if (!optContextSymbol.has_value()) {
      return {};
    }
    auto contextSymbol = optContextSymbol.value();
    if (!tagDecl->getDeclName().isEmpty()) {
      llvm::raw_string_ostream os(this->scratchBuffer);
      static_cast<const clang::NamedDecl *>(tagDecl)->printName(os);
      std::string out{fmt::format("{}{}#", contextSymbol, this->scratchBuffer)};
      this->scratchBuffer.clear();
      return out;
    }
    auto definitionTagDecl = tagDecl->getDefinition();
    ENFORCE(definitionTagDecl, "can't forward-declare an anonymous type");
    auto defLoc =
        this->sourceManager.getExpansionLoc(definitionTagDecl->getLocation());

    auto defFileId = this->sourceManager.getFileID(defLoc);
    ENFORCE(defFileId.isValid());
    auto counter = this->anonymousTypeCounters[{defFileId}]++;

    auto declContext = definitionTagDecl->getDeclContext();
    if (llvm::isa<clang::NamespaceDecl>(declContext)
        || llvm::isa<clang::TranslationUnitDecl>(declContext)) {
      // If the anonymous type is inside a namespace, then we know the
      // DeclContext chain only has namespaces (types cannot contain
      // namespaces). So include the file hash too, to avoid collisions across
      // files putting anonymous types into the same namespace. For example,
      //
      //   // A.h
      //   namespace z { struct { void f() {} } x; }
      //   // B.h
      //   namespace z { struct { int f() { return 0; } } y; }
      //
      // If we don't include the hash, the anonymous structs will end up with
      // the same symbol name.
      if (auto optRelativePath = this->getCanonicalPath(defFileId)) {
        HashValue hashValue{0};
        auto sv = optRelativePath->asStringView();
        hashValue.mix(reinterpret_cast<const uint8_t *>(sv.data()), sv.size());
        return fmt::format("{}$anontype_{:x}_{}#", contextSymbol,
                           hashValue.rawValue, counter);
      }
    }
    return fmt::format("{}$anontype_{}#", contextSymbol, counter);
  });
}

std::optional<std::string_view> SymbolFormatter::getEnumConstantSymbol(
    const clang::EnumConstantDecl *enumConstantDecl) {
  return this->getSymbolCached(enumConstantDecl, [&]() -> std::string {
    auto parentEnumDecl =
        llvm::dyn_cast<clang::EnumDecl>(enumConstantDecl->getDeclContext());
    ENFORCE(parentEnumDecl,
            "decl context for EnumConstantDecl should be EnumDecl");
    if (!parentEnumDecl) {
      return {};
    }
    std::optional<std::string_view> optContextSymbol =
        parentEnumDecl->isScoped()
            ? this->getEnumSymbol(parentEnumDecl)
            : this->getContextSymbol(parentEnumDecl->getDeclContext());
    if (!optContextSymbol.has_value()) {
      return {};
    }
    std::string out{fmt::format("{}{}.", optContextSymbol.value(),
                                toStringView(enumConstantDecl->getName()))};
    return out;
  });
}

std::optional<std::string_view>
SymbolFormatter::getEnumSymbol(const clang::EnumDecl *enumDecl) {
  return this->getTagSymbol(static_cast<const clang::TagDecl *>(enumDecl));
}

std::optional<std::string_view>
SymbolFormatter::getNamespaceSymbol(const clang::NamespaceDecl *namespaceDecl) {
  auto symbol = this->getSymbolCached(namespaceDecl, [&]() -> std::string {
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
        return "";
      }
      fmt::format_to(std::back_inserter(this->scratchBuffer), "$ANON/{}\0",
                     path->asStringView());
    } else {
      llvm::raw_string_ostream os(this->scratchBuffer);
      namespaceDecl->printQualifiedName(os);
      // Directly using string replacement is justified because the DeclContext
      // chain for a NamespaceDecl only contains namespaces or the main TU.
      // Namespaces cannot be declared inside types, functions etc.
      absl::StrReplaceAll({{"::", "/"}}, &this->scratchBuffer);
    }
    std::string out{
        fmt::format("c . todo-pkg todo-version {}/", this->scratchBuffer)};
    this->scratchBuffer.clear();
    return out;
  });
  if (symbol.empty()) {
    return std::nullopt;
  }
  return symbol;
}

} // namespace scip_clang
