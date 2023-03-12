#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

#include "clang/AST/Decl.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

#include "scip/scip.pb.h"

#include "indexer/Enforce.h"
#include "indexer/Hash.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/SymbolFormatter.h"

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
  for (auto text : {this->packageName, this->packageVersion}) {
    if (text.empty()) {
      out << ". ";
      continue;
    }
    ::addSpaceEscaped(out, text);
    out << ' ';
  }
  for (auto &descriptor : this->descriptors) {
    descriptor.formatTo(out);
  }
}

std::string
SymbolBuilder::formatContextual(std::string_view contextSymbol,
                                const DescriptorBuilder &descriptor) {
  std::string buffer;
  auto maxExtraChars = 3; // For methods, we end up adding '(', ')' and '.'
  buffer.reserve(contextSymbol.size() + descriptor.name.size()
                 + descriptor.disambiguator.size() + maxExtraChars);
  buffer.append(contextSymbol);
  llvm::raw_string_ostream os(buffer);
  descriptor.formatTo(os);
  return buffer;
}

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

  // Technically, ':' is used by SCIP for <meta>, but using ':'
  // here lines up with other situations like compiler errors.
  auto name = this->formatTemporary("{}:{}:{}", filename, defPLoc.getLine(),
                                    defPLoc.getColumn());
  std::string out{};
  SymbolBuilder{.packageName = "todo-pkg",
                .packageVersion = "todo-version",
                .descriptors = {DescriptorBuilder{
                    .name = name, .suffix = scip::Descriptor::Macro}}}
      .formatTo(out);

  auto [newIt, inserted] =
      this->locationBasedCache.insert({{defLoc}, std::move(out)});
  ENFORCE(inserted, "key was missing earlier, so insert should've succeeded");
  return std::string_view(newIt->second);
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

std::optional<std::string_view> SymbolFormatter::getSymbolCached(
    const clang::Decl *decl,
    absl::FunctionRef<std::optional<std::string>()> getSymbol) {
  ENFORCE(decl);
  // Improve cache hit ratio by using the canonical decl as the key.
  // It is a little subtle as to why using it is correct,
  // see NOTE(ref: canonical-decl)
  decl = decl->getCanonicalDecl();
  auto it = this->declBasedCache.find(decl);
  if (it != this->declBasedCache.end()) {
    return std::string_view(it->second);
  }
  auto optSymbol = getSymbol();
  if (!optSymbol.has_value()) {
    return {};
  }
  ENFORCE(!optSymbol.value().empty(),
          "forgot to use nullopt to signal failure in computing symbol name");
  auto [newIt, inserted] =
      this->declBasedCache.insert({decl, std::move(optSymbol.value())});
  ENFORCE(inserted);
  return std::string_view(newIt->second);
}

std::optional<std::string_view>
SymbolFormatter::getContextSymbol(const clang::DeclContext *declContext) {
  if (auto namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) {
    return this->getNamespaceSymbol(namespaceDecl);
  }
  if (auto tagDecl = llvm::dyn_cast<clang::TagDecl>(declContext)) {
    return this->getTagSymbol(tagDecl);
  }
  if (llvm::isa<clang::TranslationUnitDecl>(declContext)
      || llvm::isa<clang::ExternCContextDecl>(declContext)) {
    auto decl = llvm::dyn_cast<clang::Decl>(declContext);
    return this->getSymbolCached(decl, [&]() -> std::optional<std::string> {
      this->scratchBuffer.clear();
      SymbolBuilder{.packageName = "todo-pkg",
                    .packageVersion = "todo-version",
                    .descriptors = {}}
          .formatTo(this->scratchBuffer);
      return std::string(this->scratchBuffer);
    });
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
  return this->getSymbolCached(tagDecl, [&]() -> std::optional<std::string> {
    auto optContextSymbol = this->getContextSymbol(tagDecl->getDeclContext());
    if (!optContextSymbol.has_value()) {
      return {};
    }
    auto contextSymbol = optContextSymbol.value();
    if (!tagDecl->getDeclName().isEmpty()) {
      return SymbolBuilder::formatContextual(
          contextSymbol,
          DescriptorBuilder{.name = this->formatTemporary(tagDecl),
                            .suffix = scip::Descriptor::Type});
    }
    auto definitionTagDecl = tagDecl->getDefinition();
    ENFORCE(definitionTagDecl, "can't forward-declare an anonymous type");
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
      if (auto optRelativePath = this->getCanonicalPath(defFileId)) {
        descriptor.name = this->formatTemporary(
            "$anonymous_type_{:x}_{}",
            HashValue::forText(optRelativePath->asStringView()), counter);
      }
    }
    if (descriptor.name.empty()) {
      descriptor.name = this->formatTemporary("$anonymous_type_{}", counter);
    }
    return SymbolBuilder::formatContextual(contextSymbol, descriptor);
  });
}

std::optional<std::string_view> SymbolFormatter::getEnumConstantSymbol(
    const clang::EnumConstantDecl *enumConstantDecl) {
  return this->getSymbolCached(
      enumConstantDecl, [&]() -> std::optional<std::string> {
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
        return SymbolBuilder::formatContextual(
            optContextSymbol.value(),
            DescriptorBuilder{.name = toStringView(enumConstantDecl->getName()),
                              .suffix = scip::Descriptor::Term});
      });
}

std::optional<std::string_view>
SymbolFormatter::getEnumSymbol(const clang::EnumDecl *enumDecl) {
  return this->getTagSymbol(static_cast<const clang::TagDecl *>(enumDecl));
}

std::optional<std::string_view>
SymbolFormatter::getNamedDeclSymbol(const clang::NamedDecl *namedDecl) {
#define HANDLE(kind_)            \
  case clang::Decl::Kind::kind_: \
    return this->get##kind_##Symbol(llvm::cast<clang::kind_##Decl>(namedDecl));
  switch (namedDecl->getKind()) {
    HANDLE(Enum)
    HANDLE(EnumConstant)
    HANDLE(Namespace)
  default:
    return {};
  }
}

std::optional<std::string_view>
SymbolFormatter::getNamespaceSymbol(const clang::NamespaceDecl *namespaceDecl) {
  return this->getSymbolCached(
      namespaceDecl, [&]() -> std::optional<std::string> {
        auto optContextSymbol =
            this->getContextSymbol(namespaceDecl->getDeclContext());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        auto contextSymbol = optContextSymbol.value();
        DescriptorBuilder descriptor{.suffix = scip::Descriptor::Namespace};
        if (namespaceDecl->isAnonymousNamespace()) {
          auto mainFileId = this->sourceManager.getMainFileID();
          ENFORCE(mainFileId.isValid());
          auto path = this->getCanonicalPath(mainFileId);
          if (!path.has_value()) {
            // Strictly speaking, this will be suboptimal in the following case:
            // - header 1: in source tree (has canonical path), uses
            //     namespace {..}
            // - header 2: in source tree (has canonical path), uses
            //     namespace {..}
            // - generated C++ file: in build tree only (no canonical path), and
            //   includes header 1 and 2.
            // We will not emit a symbol that connects the anonymous namespace
            // in header 1 and header 2. However, that is OK, because it is
            // unclear how to handle this case anyways, and anonymous namespaces
            // are rarely (if ever) used in headers.
            return {};
          }
          descriptor.name = this->formatTemporary("$anonymous_namespace_{}",
                                                  path->asStringView());
        } else {
          descriptor.name = this->formatTemporary(namespaceDecl);
        }
        return SymbolBuilder::formatContextual(contextSymbol, descriptor);
      });
}

std::string_view
SymbolFormatter::formatTemporary(const clang::NamedDecl *namedDecl) {
  this->scratchBuffer.clear();
  llvm::raw_string_ostream os(this->scratchBuffer);
  namedDecl->printName(os);
  return std::string_view(this->scratchBuffer);
}

} // namespace scip_clang
