#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

#include "scip/scip.pb.h"

#include "indexer/DebugHelpers.h"
#include "indexer/Enforce.h"
#include "indexer/Hash.h"
#include "indexer/IdPathMappings.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"
#include "indexer/SymbolFormatter.h"
#include "indexer/SymbolName.h"

namespace {
struct Escaped {
  std::string_view text;
};
} // namespace

namespace llvm {
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Escaped s) {
  bool needsEscape = false;
  bool needsBacktickEscape = false;
  for (auto &c : s.text) {
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')
        || ('0' <= c && c <= '9') || c == '+' || c == '-' || c == '$'
        || c == '_') {
      continue;
    }
    needsBacktickEscape |= c == '`';
    needsEscape = true;
  }
  if (needsEscape) {
    if (needsBacktickEscape) {
      return os << '`' << absl::StrReplaceAll(s.text, {{"`", "``"}}) << '`';
    }
    return os << '`' << s.text << '`';
  }
  return os << s.text;
}
} // namespace llvm

static void addSpaceEscaped(llvm::raw_ostream &out, std::string_view in) {
  if (absl::StrContains(in, ' ')) {
    out << absl::StrReplaceAll(in, {{" ", "  "}});
    return;
  }
  out << in;
}

namespace scip_clang {

void DescriptorBuilder::formatTo(llvm::raw_ostream &out) const {
  // See https://github.com/sourcegraph/scip/blob/main/scip.proto#L104-L125
  switch (this->suffix) {
  case scip::Descriptor::Namespace:
    out << ::Escaped{this->name} << '/';
    break;
  case scip::Descriptor::Type:
    out << ::Escaped{this->name} << '#';
    break;
  case scip::Descriptor::Term:
    out << ::Escaped{this->name} << '.';
    break;
  case scip::Descriptor::Meta:
    out << ::Escaped{this->name} << ':';
    break;
  case scip::Descriptor::Method:
    out << ::Escaped{this->name} << '(' << ::Escaped{this->disambiguator}
        << ").";
    break;
  case scip::Descriptor::TypeParameter:
    out << '[' << ::Escaped{this->name} << ']';
    break;
  case scip::Descriptor::Parameter:
    out << '(' << ::Escaped{this->name} << ')';
    break;
  case scip::Descriptor::Macro:
    out << ::Escaped{this->name} << '!';
    break;
  default:
    ENFORCE(false, "unknown descriptor suffix %s",
            scip::Descriptor::Suffix_Name(this->suffix));
  }
}

void SymbolBuilder::formatTo(std::string &buf) const {
  llvm::raw_string_ostream out(buf);
  out << "cxx . "; // scheme manager
  if (this->packageId.name.empty()) {
    out << ". ";
  } else {
    ::addSpaceEscaped(out, this->packageId.name);
    out << ' ';
  }
  if (!this->packageId.version.empty()) {
    ::addSpaceEscaped(out, this->packageId.version);
  }
  out << "$ ";
  // NOTE(def: symbol-string-hack-for-forward-decls): Add a '$' suffix
  // after the version, but before the space for the symbol name.
  // When splitting the symbol, we check for the '$' followed by a ' '.
  for (auto &descriptor : this->descriptors) {
    descriptor.formatTo(out);
  }
}

// static
void SymbolBuilder::formatContextual(std::string &buffer,
                                     SymbolNameRef contextSymbol,
                                     const DescriptorBuilder &descriptor) {
  auto maxExtraChars = 3; // For methods, we end up adding '(', ')' and '.'
  buffer.reserve(contextSymbol.value.size() + descriptor.name.size()
                 + descriptor.disambiguator.size() + maxExtraChars);
  buffer.append(contextSymbol.value);
  llvm::raw_string_ostream os(buffer);
  descriptor.formatTo(os);
}

SymbolNameRef SymbolFormatter::format(const SymbolBuilder &symbolBuilder) {
  this->scratchBufferForSymbol.clear();
  symbolBuilder.formatTo(this->scratchBufferForSymbol);
  return {this->stringSaver.save(this->scratchBufferForSymbol)};
}

SymbolNameRef
SymbolFormatter::formatContextual(SymbolNameRef contextSymbol,
                                  const DescriptorBuilder &descriptor) {
  this->scratchBufferForSymbol.clear();
  SymbolBuilder::formatContextual(this->scratchBufferForSymbol, contextSymbol,
                                  descriptor);
  return {this->stringSaver.save(this->scratchBufferForSymbol)};
}

SymbolNameRef SymbolFormatter::formatLocal(unsigned counter) {
  this->formatTemporaryToBuf(this->scratchBufferForSymbol, "local {}", counter);
  return {this->stringSaver.save(this->scratchBufferForSymbol)};
}

SymbolNameRef SymbolFormatter::getMacroSymbol(clang::SourceLocation defLoc) {
  auto it = this->locationBasedCache.find({defLoc});
  if (it != this->locationBasedCache.end()) {
    return it->second;
  }
  // Ignore line directives here because we care about the identity
  // of the macro (based on the containing file), not where it
  // originated from.
  auto defPLoc =
      this->sourceManager.getPresumedLoc(defLoc, /*UseLineDirectives*/ false);
  ENFORCE(defPLoc.isValid());
  std::string_view filepath;
  PackageId packageId{};
  if (auto *fileMetadata =
          this->fileMetadataMap.getFileMetadata(defPLoc.getFileID())) {
    filepath = fileMetadata->stableFileId.path.asStringView();
    packageId = fileMetadata->packageId();
  } else {
    filepath = std::string_view(defPLoc.getFilename());
  }

  // Technically, ':' is used by SCIP for <meta>, but using ':'
  // here lines up with other situations like compiler errors.
  auto name = this->formatTemporaryName("{}:{}:{}", filepath, defPLoc.getLine(),
                                        defPLoc.getColumn());
  auto symbol = this->format(SymbolBuilder{
      packageId,
      {DescriptorBuilder{.name = name, .suffix = scip::Descriptor::Macro}}});

  auto [newIt, inserted] = this->locationBasedCache.insert({{defLoc}, symbol});
  ENFORCE(inserted, "key was missing earlier, so insert should've succeeded");
  return newIt->second;
}

SymbolNameRef SymbolFormatter::getFileSymbol(const FileMetadata &fileMetadata) {
  auto &stableFileId = fileMetadata.stableFileId;
  auto it = this->fileSymbolCache.find(stableFileId);
  if (it != this->fileSymbolCache.end()) {
    return it->second;
  }
  auto name =
      this->formatTemporaryName("<file>/{}", stableFileId.path.asStringView());
  auto packageId = fileMetadata.packageId();
  auto symbol = this->format(
      SymbolBuilder{packageId,
                    {DescriptorBuilder{
                        .name = name, .suffix = scip::Descriptor::Namespace}}});

  auto [newIt, inserted] = this->fileSymbolCache.emplace(stableFileId, symbol);
  ENFORCE(
      inserted,
      "StableFileId key was missing earlier, so insert should've succeeded");
  return newIt->second;
}

// NOTE(def: canonical-decl):
// It is a little subtle as to why using getCanonicalDecl will
// give correct results. In particular, the result of getCanonicalDecl
// may change depending on include order. For example, if you have:
//
//   void f(int x); // In A.h
//   void f(int x); // In B.h
//
// Then depending on #include order of A.h and B.h, the result of
// getCanonicalDecl will be different, since tie-breaking is based on
// lexical order if all declarations are forward declarations.
//
// Say we have two TUs which include these headers in different order:
//
//   // AThenB.cc
//   #include "A.h"
//   #include "B.h"
//
//   // BThenA.cc
//   #include "B.h"
//   #include "A.h"
//
// In this case, when AThenB.cc is indexed, the declaration from A.h
// will be returned by getCanonicalDecl, whereas when BThenA.h is indexed,
// the declaration from B.h will be returned.
//
// Consequently, if header paths are present in symbol names,
// and we use the result for making symbol names,
// then the two separate indexing processes will generate
// two different symbol names, which is undesirable.
//
// NOTE(ref: symbol-names-for-decls) points out that header
// names are only a part of symbol names in two cases:
// 1. Macros
// 2. Anonymous types (and hence, declarations inside anonymous types)
//
// In both of these cases, forward declarations are not possible,
// so include ordering doesn't matter, and consequently using
// getCanonicalDecl is fine.

std::optional<SymbolNameRef> SymbolFormatter::getSymbolCached(
    const clang::Decl &decl,
    absl::FunctionRef<std::optional<SymbolNameRef>()> getSymbol) {
  // Improve cache hit ratio by using the canonical decl as the key.
  // It is a little subtle as to why using it is correct,
  // see NOTE(ref: canonical-decl)
  auto *canonicalDecl = decl.getCanonicalDecl();
  auto it = this->declBasedCache.find(canonicalDecl);
  if (it != this->declBasedCache.end()) {
    return it->second;
  }
  auto optSymbol = getSymbol();
  if (!optSymbol.has_value()) {
    return {};
  }
  ENFORCE(!optSymbol.value().value.empty(),
          "forgot to use nullopt to signal failure in computing symbol name");
  auto [newIt, inserted] =
      this->declBasedCache.insert({canonicalDecl, *optSymbol});
  ENFORCE(inserted);
  return newIt->second;
}

std::optional<SymbolNameRef> SymbolFormatter::getSymbolCached(
    const clang::Decl *decl, clang::SourceLocation loc,
    absl::FunctionRef<std::optional<SymbolNameRef>()> getSymbol) {
  auto fileId = this->sourceManager.getFileID(loc);
  if (decl) {
    decl = decl->getCanonicalDecl();
  }
  using KeyType =
      std::pair<const clang::Decl *, llvm_ext::AbslHashAdapter<clang::FileID>>;
  auto it = this->namespacePrefixCache.find(KeyType{decl, {fileId}});
  if (it != this->namespacePrefixCache.end()) {
    return it->second;
  }
  auto optSymbol = getSymbol();
  if (!optSymbol.has_value()) {
    return {};
  }
  ENFORCE(!optSymbol.value().value.empty(),
          "forgot to use nullopt to signal failure in compute symbol name");
  auto [newIt, inserted] =
      this->namespacePrefixCache.emplace(KeyType{decl, {fileId}}, *optSymbol);
  ENFORCE(inserted);
  return newIt->second;
}

std::optional<SymbolNameRef> SymbolFormatter::getNamespaceSymbolPrefix(
    const clang::NamespaceDecl &namespaceDecl, clang::SourceLocation loc) {
  return this->getSymbolCached(
      &namespaceDecl, loc, [&]() -> std::optional<SymbolNameRef> {
        auto optContextSymbol =
            this->getContextSymbol(*namespaceDecl.getDeclContext(), loc);
        if (!optContextSymbol.has_value()) {
          return {};
        }
        auto contextSymbol = optContextSymbol.value();
        DescriptorBuilder descriptor{.suffix = scip::Descriptor::Namespace};
        if (namespaceDecl.isAnonymousNamespace()) {
          auto mainFileId = this->sourceManager.getMainFileID();
          ENFORCE(mainFileId.isValid());
          auto optStableFileId =
              this->fileMetadataMap.getStableFileId(mainFileId);
          ENFORCE(optStableFileId.has_value(),
                  "main file always has a valid StableFileId");
          auto path = optStableFileId->path;
          descriptor.name = this->formatTemporaryName("$anonymous_namespace_{}",
                                                      path.asStringView());
        } else {
          descriptor.name = this->formatTemporaryName(namespaceDecl);
        }
        return this->formatContextual(contextSymbol, descriptor);
      });
}

std::optional<SymbolNameRef>
SymbolFormatter::getLocationBasedSymbolPrefix(clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return {};
  }
  return this->getSymbolCached(
      nullptr, loc, [&]() -> std::optional<SymbolNameRef> {
        auto fileId = this->sourceManager.getFileID(loc);
        auto *fileMetadata = this->fileMetadataMap.getFileMetadata(fileId);
        if (!fileMetadata) {
          return {};
        }
        auto packageId = fileMetadata->packageId();
        return this->format(SymbolBuilder{packageId, /*descriptors*/ {}});
      });
}

std::optional<SymbolNameRef>
SymbolFormatter::getContextSymbol(const clang::DeclContext &declContext,
                                  clang::SourceLocation loc) {
  loc = this->sourceManager.getExpansionLoc(loc);
  if (auto *namespaceDecl =
          llvm::dyn_cast<clang::NamespaceDecl>(&declContext)) {
    return this->getNamespaceSymbolPrefix(*namespaceDecl, loc);
  }
  if (auto *tagDecl = llvm::dyn_cast<clang::TagDecl>(&declContext)) {
    return this->getTagSymbol(*tagDecl);
  }
  if (llvm::isa<clang::TranslationUnitDecl>(&declContext)
      || llvm::isa<clang::ExternCContextDecl>(&declContext)) {
    return this->getLocationBasedSymbolPrefix(loc);
  }
  if (auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(&declContext)) {
    // TODO: Strictly speaking, we should return some information marking
    // the symbol as local, but it shouldn't be possible to create spurious
    // references, so this is OK for now.
    return this->getFunctionSymbol(*functionDecl);
  }
  // TODO: Handle all cases of DeclContext here:
  // Done
  // - TranslationUnitDecl
  // - ExternCContext
  // - NamespaceDecl
  // - TagDecl
  // - FunctionDecl
  // Pending:
  // - OMPDeclareReductionDecl
  // - OMPDeclareMapperDecl
  // - ObjCMethodDecl
  // - ObjCContainerDecl
  // - LinkageSpecDecl
  // - ExportDecl
  // - BlockDecl
  // - CapturedDecl
  return std::nullopt;
}

std::optional<SymbolNameRef>
SymbolFormatter::getRecordSymbol(const clang::RecordDecl &recordDecl) {
  return this->getTagSymbol(recordDecl);
}

std::optional<SymbolNameRef>
SymbolFormatter::getTagSymbol(const clang::TagDecl &tagDecl) {
  return this->getSymbolCached(tagDecl, [&]() -> std::optional<SymbolNameRef> {
    auto optContextSymbol = this->getContextSymbol(*tagDecl.getDeclContext(),
                                                   tagDecl.getLocation());
    if (!optContextSymbol.has_value()) {
      return {};
    }
    auto contextSymbol = optContextSymbol.value();
    if (!tagDecl.getDeclName().isEmpty()) {
      return this->formatContextual(
          contextSymbol,
          DescriptorBuilder{.name = this->formatTemporaryName(tagDecl),
                            .suffix = scip::Descriptor::Type});
    }

    auto *definitionTagDecl = tagDecl.getDefinition();
    if (!definitionTagDecl) {
      // NOTE(def: missing-definition-for-tagdecl)
      // Intuitively, it seems like this case where an anonymous type
      // lacks a definition should be impossible (you can't forward declare
      // such a type), but this case is triggered when indexing LLVM.
      return {};
    }
    auto defLoc =
        this->sourceManager.getExpansionLoc(definitionTagDecl->getLocation());

    auto defFileId = this->sourceManager.getFileID(defLoc);
    ENFORCE(defFileId.isValid());
    auto counter = this->anonymousTypeCounters[{defFileId}]++;

    auto declContext = definitionTagDecl->getDeclContext();
    DescriptorBuilder descriptor{.name = {}, .suffix = scip::Descriptor::Type};
    if (llvm::isa<clang::NamespaceDecl>(declContext)
        || llvm::isa<clang::TranslationUnitDecl>(declContext)) {
      // If the anonymous type is inside a namespace, then we know the
      // DeclContext chain only has namespaces (types cannot contain
      // namespaces). So include the file path hash too, to avoid collisions
      // across files putting anonymous types into the same namespace.
      // For example,
      //
      //   // A.h
      //   namespace z { struct { void f() {} } x; }
      //   // B.h
      //   namespace z { struct { int f() { return 0; } } y; }
      //
      // If we don't include the hash, the anonymous structs will end up with
      // the same symbol name.
      if (auto optStableFileId =
              this->fileMetadataMap.getStableFileId(defFileId)) {
        descriptor.name = this->formatTemporaryName(
            "$anonymous_type_{:x}_{}",
            HashValue::forText(optStableFileId->path.asStringView()), counter);
      }
    }
    if (descriptor.name.empty()) {
      descriptor.name =
          this->formatTemporaryName("$anonymous_type_{}", counter);
    }
    return this->formatContextual(contextSymbol, descriptor);
  });
}

std::optional<SymbolNameRef>
SymbolFormatter::getBindingSymbol(const clang::BindingDecl &bindingDecl) {
  return this->getNextLocalSymbol(bindingDecl);
}

std::optional<SymbolNameRef> SymbolFormatter::getClassTemplateSymbol(
    const clang::ClassTemplateDecl &classTemplateDecl) {
  return this->getRecordSymbol(*classTemplateDecl.getTemplatedDecl());
}

std::optional<SymbolNameRef> SymbolFormatter::getTypeAliasTemplateSymbol(
    const clang::TypeAliasTemplateDecl &typeAliasTemplateDecl) {
  return this->getTypedefNameSymbol(*typeAliasTemplateDecl.getTemplatedDecl());
}

std::optional<SymbolNameRef>
SymbolFormatter::getNextLocalSymbol(const clang::NamedDecl &decl) {
  if (decl.getDeclName().isEmpty()) {
    return {};
  }
  return this->getSymbolCached(decl, [&]() -> std::optional<SymbolNameRef> {
    auto loc = this->sourceManager.getExpansionLoc(decl.getLocation());
    auto defFileId = this->sourceManager.getFileID(loc);
    auto counter = this->localVariableCounters[{defFileId}]++;
    return this->formatLocal(counter);
  });
}

std::optional<SymbolNameRef> SymbolFormatter::getEnumConstantSymbol(
    const clang::EnumConstantDecl &enumConstantDecl) {
  return this->getSymbolCached(
      enumConstantDecl, [&]() -> std::optional<SymbolNameRef> {
        auto parentEnumDecl =
            llvm::dyn_cast<clang::EnumDecl>(enumConstantDecl.getDeclContext());
        ENFORCE(parentEnumDecl,
                "decl context for EnumConstantDecl should be EnumDecl");
        if (!parentEnumDecl) {
          return {};
        }
        std::optional<SymbolNameRef> optContextSymbol =
            parentEnumDecl->isScoped()
                ? this->getEnumSymbol(*parentEnumDecl)
                : this->getContextSymbol(*parentEnumDecl->getDeclContext(),
                                         enumConstantDecl.getLocation());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        return this->formatContextual(
            optContextSymbol.value(),
            DescriptorBuilder{
                .name = llvm_ext::toStringView(enumConstantDecl.getName()),
                .suffix = scip::Descriptor::Term});
      });
}

std::optional<SymbolNameRef>
SymbolFormatter::getEnumSymbol(const clang::EnumDecl &enumDecl) {
  return this->getTagSymbol(enumDecl);
}

std::string_view SymbolFormatter::getFunctionDisambiguator(
    const clang::FunctionDecl &functionDecl, char buf[16]) {
  const clang::FunctionDecl *definingDecl = &functionDecl;
  // clang-format off
    if (functionDecl.isTemplateInstantiation()) {
      // Handle non-templated member functions
      if (auto *memberFnDecl = functionDecl.getInstantiatedFromMemberFunction()) {
        definingDecl = memberFnDecl;
      } else if (auto *templateInfo = functionDecl.getTemplateSpecializationInfo()) {
        // Consider code like:
        //   template <typename T> class C { template <typename U> void f() {} };
        //   void g() { C<int>().f<int>(); }
        //                       ^ Emitting a reference
        //
        // The dance below gets to the original declaration in 3 steps:
        // C<int>.f<int> (FunctionDecl) → C<int>.f<$U> (FunctionTemplateDecl)
        //                                     ↓
        // C<$T>.f<$U>   (FunctionDecl) ← C<$T>.f<$U>  (FunctionTemplateDecl)
        auto *instantiatedTemplateDecl = templateInfo->getTemplate();
        // For some reason, we end up on this code path for overloaded
        // literal operators. In that case, uninstantiatedTemplateDecl
        // can be null.
        if (auto *uninstantiatedTemplateDecl = instantiatedTemplateDecl->getInstantiatedFromMemberTemplate()) {
          definingDecl = uninstantiatedTemplateDecl->getTemplatedDecl();
        }
      }
    }
  // clang-format on
  // 64-bit hash in hex should take 16 characters at most.
  auto typeString = definingDecl->getType().getCanonicalType().getAsString();
  // char buf[16] = {0};
  auto *end = fmt::format_to(buf, "{:x}", HashValue::forText(typeString));
  return std::string_view{buf, end};
}

std::optional<SymbolNameRef>
SymbolFormatter::getFunctionSymbol(const clang::FunctionDecl &functionDecl) {
  return this->getSymbolCached(
      functionDecl, [&]() -> std::optional<SymbolNameRef> {
        auto optContextSymbol = this->getContextSymbol(
            *functionDecl.getDeclContext(), functionDecl.getLocation());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        auto name = this->formatTemporaryName(functionDecl);
        char buf[16] = {0};
        auto disambiguator = this->getFunctionDisambiguator(functionDecl, buf);
        return this->formatContextual(optContextSymbol.value(),
                                      DescriptorBuilder{
                                          .name = name,
                                          .disambiguator = disambiguator,
                                          .suffix = scip::Descriptor::Method,
                                      });
      });
}

std::optional<SymbolNameRef> SymbolFormatter::getFunctionTemplateSymbol(
    const clang::FunctionTemplateDecl &functionTemplateDecl) {
  return this->getFunctionSymbol(*functionTemplateDecl.getTemplatedDecl());
}

std::optional<SymbolNameRef>
SymbolFormatter::getFieldSymbol(const clang::FieldDecl &fieldDecl) {
  if (fieldDecl.getDeclName().isEmpty()) {
    return {};
  }
  return this->getSymbolCached(
      fieldDecl, [&]() -> std::optional<SymbolNameRef> {
        auto optContextSymbol = this->getContextSymbol(
            *fieldDecl.getDeclContext(), fieldDecl.getLocation());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        return this->formatContextual(
            optContextSymbol.value(),
            DescriptorBuilder{
                .name = llvm_ext::toStringView(fieldDecl.getName()),
                .suffix = scip::Descriptor::Term,
            });
      });
}

std::optional<SymbolNameRef>
SymbolFormatter::getNamedDeclSymbol(const clang::NamedDecl &namedDecl) {
#define HANDLE(kind_)                                                \
  if (auto *decl = llvm::dyn_cast<clang::kind_##Decl>(&namedDecl)) { \
    return this->get##kind_##Symbol(*decl);                          \
  }
  FOR_EACH_DECL_TO_BE_INDEXED(HANDLE)
  return {};
}

/// Returns nullopt for anonymous namespaces in files for which
/// getCanonicalPath returns nullopt.
std::optional<SymbolNameRef>
SymbolFormatter::getNamespaceSymbol(const clang::NamespaceDecl &namespaceDecl) {
  return this->getNamespaceSymbolPrefix(namespaceDecl,
                                        namespaceDecl.getLocation());
}

std::optional<SymbolNameRef>
SymbolFormatter::getLocalVarOrParmSymbol(const clang::VarDecl &varDecl) {
  ENFORCE(varDecl.isLocalVarDeclOrParm());
  return this->getNextLocalSymbol(varDecl);
}

#define GET_AS_LOCAL(name_)                                         \
  std::optional<SymbolNameRef> SymbolFormatter::get##name_##Symbol( \
      const clang::name_##Decl &decl) {                             \
    return this->getNextLocalSymbol(decl);                          \
  }
FOR_EACH_TEMPLATE_PARM_TO_BE_INDEXED(GET_AS_LOCAL)
#undef GET_AS_LOCAL

std::optional<SymbolNameRef> SymbolFormatter::getTypedefNameSymbol(
    const clang::TypedefNameDecl &typedefNameDecl) {
  return this->getSymbolCached(
      typedefNameDecl, [&]() -> std::optional<SymbolNameRef> {
        auto optContextSymbol = this->getContextSymbol(
            *typedefNameDecl.getDeclContext(), typedefNameDecl.getLocation());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        return this->formatContextual(
            *optContextSymbol,
            DescriptorBuilder{
                .name = llvm_ext::toStringView(typedefNameDecl.getName()),
                .suffix = scip::Descriptor::Type,
            });
      });
}

std::optional<SymbolNameRef> SymbolFormatter::getUsingShadowSymbol(
    const clang::UsingShadowDecl &usingShadowDecl) {
  if (usingShadowDecl.getDeclName().isEmpty()) {
    return {};
  }
  auto *canonicalDecl = usingShadowDecl.getUnderlyingDecl()->getCanonicalDecl();
  if (!canonicalDecl) {
    return {};
  }
  return this->getSymbolCached(
      usingShadowDecl, [&]() -> std::optional<SymbolNameRef> {
        auto optContextSymbol = this->getContextSymbol(
            *usingShadowDecl.getDeclContext(), usingShadowDecl.getLocation());
        if (!optContextSymbol) {
          return {};
        }
        scip::Descriptor::Suffix suffix;
        char buf[16] = {0};
        std::string_view disambiguator = "";
        // NOTE: First two branches can't be re-ordered as all
        // TemplateTypeParmDecls also TypeDecls
        // clang-format off
        do {
          if (llvm::dyn_cast<clang::TemplateTypeParmDecl>(canonicalDecl)) {
            suffix = scip::Descriptor::TypeParameter;
          } else if (llvm::isa<clang::TypeDecl>(canonicalDecl)
                    || llvm::isa<clang::ClassTemplateDecl>(canonicalDecl)) {
            suffix = scip::Descriptor::Type;
          } else if (llvm::dyn_cast<clang::NamespaceDecl>(canonicalDecl)) {
            suffix = scip::Descriptor::Namespace;
          } else if (llvm::isa<clang::EnumConstantDecl>(canonicalDecl)
                    || llvm::isa<clang::VarDecl>(canonicalDecl)
                    || llvm::isa<clang::VarTemplateDecl>(canonicalDecl)) {
            suffix = scip::Descriptor::Term;
          } else if (auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(canonicalDecl)) {
            disambiguator = this->getFunctionDisambiguator(*functionDecl, buf);
            suffix = scip::Descriptor::Method;
          } else if (auto *funcTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(canonicalDecl)) {
            disambiguator = this->getFunctionDisambiguator(*funcTemplateDecl->getTemplatedDecl(), buf);
            suffix = scip::Descriptor::Method;
          } else if (auto *usingTemplateDecl = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(canonicalDecl)) {
            canonicalDecl = usingTemplateDecl->getTemplatedDecl();
            continue;
          } else {
            return {};
          }
          break;
        } while (true);
        // clang-format on
        auto descriptor = DescriptorBuilder{
            .name = this->formatTemporaryName(usingShadowDecl),
            .disambiguator = disambiguator,
            .suffix = suffix,
        };
        return this->formatContextual(*optContextSymbol, descriptor);
      });
}

std::optional<SymbolNameRef>
SymbolFormatter::getUsingSymbol(const clang::UsingDecl &) {
  ENFORCE(false, "call getUsingShadowSymbol instead");
  return {};
}

std::optional<SymbolNameRef>
SymbolFormatter::getVarSymbol(const clang::VarDecl &varDecl) {
  if (varDecl.isLocalVarDeclOrParm() && !varDecl.isLocalExternDecl()) {
    return this->getLocalVarOrParmSymbol(varDecl);
  }
  if (varDecl.getDeclName().isEmpty()) {
    return {};
  }
  return this->getSymbolCached(varDecl, [&]() -> std::optional<SymbolNameRef> {
    using Kind = clang::Decl::Kind;
    // Based on
    // https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/include/clang/Basic/DeclNodes.td?L57-64
    switch (varDecl.getKind()) {
    case Kind::Decomposition:
      ENFORCE(false, "DecompositionDecls require recursive traversal"
                     " and do not have a single symbol name;"
                     " they should be handled in TuIndexer");
      return {}; // in release mode
    case Kind::ParmVar:
      ENFORCE(false, "already handled parameter case earlier");
      return {}; // in release mode
    case Kind::VarTemplatePartialSpecialization:
    case Kind::VarTemplateSpecialization:
    case Kind::Var: {
      if (auto optContextSymbol = this->getContextSymbol(
              *varDecl.getDeclContext(), varDecl.getLocation())) {
        auto descriptor = DescriptorBuilder{
            .name = llvm_ext::toStringView(varDecl.getName()),
            .suffix = scip::Descriptor::Term,
        };
        return this->formatContextual(*optContextSymbol, descriptor);
      }
      return {};
    }
    case Kind::OMPCapturedExpr:
      return {};
    default: {
      spdlog::warn("unhandled kind {} of VarDecl: {}",
                   varDecl.getDeclKindName(), debug::formatDecl(&varDecl));
      return {};
    }
    }
  });
}

std::optional<SymbolNameRef> SymbolFormatter::getVarTemplateSymbol(
    const clang::VarTemplateDecl &varTemplateDecl) {
  return this->getVarSymbol(*varTemplateDecl.getTemplatedDecl());
}

std::string_view
SymbolFormatter::formatTemporaryName(const clang::NamedDecl &namedDecl) {
  this->scratchBufferForName.clear();
  llvm::raw_string_ostream os(this->scratchBufferForName);
  namedDecl.printName(os);
  return std::string_view(this->scratchBufferForName);
}

} // namespace scip_clang
