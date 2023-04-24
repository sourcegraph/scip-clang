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

// static
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
  std::string_view filepath;
  if (auto optStableFileId = this->getStableFileId(defPLoc.getFileID())) {
    filepath = optStableFileId->path.asStringView();
  } else {
    filepath = std::string_view(defPLoc.getFilename());
  }

  // Technically, ':' is used by SCIP for <meta>, but using ':'
  // here lines up with other situations like compiler errors.
  auto name = this->formatTemporary("{}:{}:{}", filepath, defPLoc.getLine(),
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

std::string_view SymbolFormatter::getFileSymbol(StableFileId stableFileId) {
  auto it = this->fileSymbolCache.find(stableFileId);
  if (it != this->fileSymbolCache.end()) {
    return std::string_view(it->second);
  }
  auto name =
      this->formatTemporary("<file>/{}", stableFileId.path.asStringView());
  std::string out{};
  SymbolBuilder{.packageName = "todo-pkg",
                .packageVersion = "todo-version",
                .descriptors = {DescriptorBuilder{
                    .name = name, .suffix = scip::Descriptor::Namespace}}}
      .formatTo(out);
  auto [newIt, inserted] =
      this->fileSymbolCache.emplace(stableFileId, std::move(out));
  ENFORCE(
      inserted,
      "StableFileId key was missing earlier, so insert should've succeeded");
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
    const clang::Decl &decl,
    absl::FunctionRef<std::optional<std::string>()> getSymbol) {
  // Improve cache hit ratio by using the canonical decl as the key.
  // It is a little subtle as to why using it is correct,
  // see NOTE(ref: canonical-decl)
  auto *canonicalDecl = decl.getCanonicalDecl();
  auto it = this->declBasedCache.find(canonicalDecl);
  if (it != this->declBasedCache.end()) {
    return std::string_view(it->second);
  }
  auto optSymbol = getSymbol();
  if (!optSymbol.has_value()) {
    return {};
  }
  ENFORCE(!optSymbol.value().empty(),
          "forgot to use nullopt to signal failure in computing symbol name");
  auto [newIt, inserted] = this->declBasedCache.insert(
      {canonicalDecl, std::move(optSymbol.value())});
  ENFORCE(inserted);
  return std::string_view(newIt->second);
}

std::optional<std::string_view>
SymbolFormatter::getContextSymbol(const clang::DeclContext &declContext) {
  if (auto namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(&declContext)) {
    return this->getNamespaceSymbol(*namespaceDecl);
  }
  if (auto tagDecl = llvm::dyn_cast<clang::TagDecl>(&declContext)) {
    return this->getTagSymbol(*tagDecl);
  }
  if (llvm::isa<clang::TranslationUnitDecl>(declContext)
      || llvm::isa<clang::ExternCContextDecl>(declContext)) {
    auto decl = llvm::dyn_cast<clang::Decl>(&declContext);
    return this->getSymbolCached(*decl, [&]() -> std::optional<std::string> {
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
SymbolFormatter::getRecordSymbol(const clang::RecordDecl &recordDecl) {
  return this->getTagSymbol(recordDecl);
}

std::optional<std::string_view>
SymbolFormatter::getTagSymbol(const clang::TagDecl &tagDecl) {
  return this->getSymbolCached(tagDecl, [&]() -> std::optional<std::string> {
    auto optContextSymbol = this->getContextSymbol(*tagDecl.getDeclContext());
    if (!optContextSymbol.has_value()) {
      return {};
    }
    auto contextSymbol = optContextSymbol.value();
    if (!tagDecl.getDeclName().isEmpty()) {
      return SymbolBuilder::formatContextual(
          contextSymbol,
          DescriptorBuilder{.name = this->formatTemporary(tagDecl),
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
      if (auto optStableFileId = this->getStableFileId(defFileId)) {
        descriptor.name = this->formatTemporary(
            "$anonymous_type_{:x}_{}",
            HashValue::forText(optStableFileId->path.asStringView()), counter);
      }
    }
    if (descriptor.name.empty()) {
      descriptor.name = this->formatTemporary("$anonymous_type_{}", counter);
    }
    return SymbolBuilder::formatContextual(contextSymbol, descriptor);
  });
}

std::optional<std::string_view>
SymbolFormatter::getBindingSymbol(const clang::BindingDecl &bindingDecl) {
  return this->getNextLocalSymbol(bindingDecl);
}

std::optional<std::string_view>
SymbolFormatter::getNextLocalSymbol(const clang::NamedDecl &decl) {
  if (decl.getDeclName().isEmpty()) {
    return {};
  }
  return this->getSymbolCached(decl, [&]() -> std::optional<std::string> {
    auto loc = this->sourceManager.getExpansionLoc(decl.getLocation());
    auto defFileId = this->sourceManager.getFileID(loc);
    auto counter = this->localVariableCounters[{defFileId}]++;
    return std::string(this->formatTemporary("local {}", counter));
  });
}

std::optional<std::string_view> SymbolFormatter::getEnumConstantSymbol(
    const clang::EnumConstantDecl &enumConstantDecl) {
  return this->getSymbolCached(
      enumConstantDecl, [&]() -> std::optional<std::string> {
        auto parentEnumDecl =
            llvm::dyn_cast<clang::EnumDecl>(enumConstantDecl.getDeclContext());
        ENFORCE(parentEnumDecl,
                "decl context for EnumConstantDecl should be EnumDecl");
        if (!parentEnumDecl) {
          return {};
        }
        std::optional<std::string_view> optContextSymbol =
            parentEnumDecl->isScoped()
                ? this->getEnumSymbol(*parentEnumDecl)
                : this->getContextSymbol(*parentEnumDecl->getDeclContext());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        return SymbolBuilder::formatContextual(
            optContextSymbol.value(),
            DescriptorBuilder{
                .name = llvm_ext::toStringView(enumConstantDecl.getName()),
                .suffix = scip::Descriptor::Term});
      });
}

std::optional<std::string_view>
SymbolFormatter::getEnumSymbol(const clang::EnumDecl &enumDecl) {
  return this->getTagSymbol(enumDecl);
}

std::optional<std::string_view>
SymbolFormatter::getFunctionSymbol(const clang::FunctionDecl &functionDecl) {
  return this->getSymbolCached(
      functionDecl, [&]() -> std::optional<std::string> {
        auto optContextSymbol =
            this->getContextSymbol(*functionDecl.getDeclContext());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        // See discussion in docs/Design.md for this choice of disambiguator.
        auto typeString =
            functionDecl.getType().getCanonicalType().getAsString();
        auto name = functionDecl.getNameAsString();
        return SymbolBuilder::formatContextual(
            optContextSymbol.value(),
            DescriptorBuilder{
                .name = name,
                .disambiguator = this->formatTemporary(
                    "{:x}", HashValue::forText(typeString)),
                .suffix = scip::Descriptor::Method,
            });
      });
}

std::optional<std::string_view>
SymbolFormatter::getFieldSymbol(const clang::FieldDecl &fieldDecl) {
  if (fieldDecl.getDeclName().isEmpty()) {
    return {};
  }
  return this->getSymbolCached(fieldDecl, [&]() -> std::optional<std::string> {
    auto optContextSymbol = this->getContextSymbol(*fieldDecl.getDeclContext());
    if (!optContextSymbol.has_value()) {
      return {};
    }
    return SymbolBuilder::formatContextual(
        optContextSymbol.value(),
        DescriptorBuilder{
            .name = llvm_ext::toStringView(fieldDecl.getName()),
            .suffix = scip::Descriptor::Term,
        });
  });
}

std::optional<std::string_view>
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
std::optional<std::string_view>
SymbolFormatter::getNamespaceSymbol(const clang::NamespaceDecl &namespaceDecl) {
  return this->getSymbolCached(
      namespaceDecl, [&]() -> std::optional<std::string> {
        auto optContextSymbol =
            this->getContextSymbol(*namespaceDecl.getDeclContext());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        auto contextSymbol = optContextSymbol.value();
        DescriptorBuilder descriptor{.suffix = scip::Descriptor::Namespace};
        if (namespaceDecl.isAnonymousNamespace()) {
          auto mainFileId = this->sourceManager.getMainFileID();
          ENFORCE(mainFileId.isValid());
          auto optStableFileId = this->getStableFileId(mainFileId);
          ENFORCE(optStableFileId.has_value(),
                  "main file always has a valid StableFileId");
          auto path = optStableFileId->path;
          descriptor.name = this->formatTemporary("$anonymous_namespace_{}",
                                                  path.asStringView());
        } else {
          descriptor.name = this->formatTemporary(namespaceDecl);
        }
        return SymbolBuilder::formatContextual(contextSymbol, descriptor);
      });
}

std::optional<std::string_view>
SymbolFormatter::getLocalVarOrParmSymbol(const clang::VarDecl &varDecl) {
  ENFORCE(varDecl.isLocalVarDeclOrParm());
  return this->getNextLocalSymbol(varDecl);
}

#define GET_AS_LOCAL(name_)                                            \
  std::optional<std::string_view> SymbolFormatter::get##name_##Symbol( \
      const clang::name_##Decl &decl) {                                \
    return this->getNextLocalSymbol(decl);                             \
  }
FOR_EACH_TEMPLATE_PARM_TO_BE_INDEXED(GET_AS_LOCAL)
#undef GET_AS_LOCAL

std::optional<std::string_view> SymbolFormatter::getTypedefNameSymbol(
    const clang::TypedefNameDecl &typedefNameDecl) {
  return this->getSymbolCached(
      typedefNameDecl, [&]() -> std::optional<std::string> {
        auto optContextSymbol =
            this->getContextSymbol(*typedefNameDecl.getDeclContext());
        if (!optContextSymbol.has_value()) {
          return {};
        }
        return SymbolBuilder::formatContextual(
            *optContextSymbol,
            DescriptorBuilder{
                .name = llvm_ext::toStringView(typedefNameDecl.getName()),
                .suffix = scip::Descriptor::Type,
            });
      });
}

std::optional<std::string_view> SymbolFormatter::getUsingShadowSymbol(
    const clang::UsingShadowDecl &usingShadowDecl) {
  if (usingShadowDecl.getDeclName().isEmpty()) {
    return {};
  }
  auto *canonicalDecl = usingShadowDecl.getUnderlyingDecl()->getCanonicalDecl();
  if (!canonicalDecl) {
    return {};
  }
  return this->getSymbolCached(
      usingShadowDecl, [&]() -> std::optional<std::string> {
        auto optContextSymbol =
            this->getContextSymbol(*usingShadowDecl.getDeclContext());
        if (!optContextSymbol) {
          return {};
        }
        scip::Descriptor::Suffix suffix;
        // NOTE: First two branches can't be re-ordered as all
        // TemplateTypeParmDecls also TypeDecls
        if (llvm::dyn_cast<clang::TemplateTypeParmDecl>(canonicalDecl)) {
          suffix = scip::Descriptor::TypeParameter;
        } else if (llvm::dyn_cast<clang::TypeDecl>(canonicalDecl)) {
          suffix = scip::Descriptor::Type;
        } else if (llvm::dyn_cast<clang::NamespaceDecl>(canonicalDecl)) {
          suffix = scip::Descriptor::Namespace;
        } else if (llvm::dyn_cast<clang::EnumConstantDecl>(canonicalDecl)
                   || llvm::dyn_cast<clang::FieldDecl>(canonicalDecl)) {
          suffix = scip::Descriptor::Term;
        } else if (llvm::dyn_cast<clang::FunctionDecl>(canonicalDecl)) {
          suffix = scip::Descriptor::Method;
        } else {
          return {};
        }
        auto descriptor = DescriptorBuilder{
            .name = llvm_ext::toStringView(usingShadowDecl.getName()),
            .suffix = suffix,
        };
        return SymbolBuilder::formatContextual(*optContextSymbol, descriptor);
      });
}

std::optional<std::string_view>
SymbolFormatter::getUsingSymbol(const clang::UsingDecl &) {
  ENFORCE(false, "call getUsingShadowSymbol instead");
  return {};
}

std::optional<std::string_view>
SymbolFormatter::getVarSymbol(const clang::VarDecl &varDecl) {
  if (varDecl.isLocalVarDeclOrParm()) {
    return this->getLocalVarOrParmSymbol(varDecl);
  }
  if (varDecl.getDeclName().isEmpty()) {
    return {};
  }
  return this->getSymbolCached(varDecl, [&]() -> std::optional<std::string> {
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
      if (auto optContextSymbol =
              this->getContextSymbol(*varDecl.getDeclContext())) {
        auto descriptor = DescriptorBuilder{
            .name = llvm_ext::toStringView(varDecl.getName()),
            .suffix = scip::Descriptor::Term,
        };
        return SymbolBuilder::formatContextual(*optContextSymbol, descriptor);
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

std::string_view
SymbolFormatter::formatTemporary(const clang::NamedDecl &namedDecl) {
  this->scratchBuffer.clear();
  llvm::raw_string_ostream os(this->scratchBuffer);
  namedDecl.printName(os);
  return std::string_view(this->scratchBuffer);
}

} // namespace scip_clang
