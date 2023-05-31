#include <compare>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/function_ref.h"
#include "perfetto/perfetto.h"

#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"

#include "proto/fwd_decls.pb.h"
#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/Comparison.h"
#include "indexer/Enforce.h"
#include "indexer/ScipExtras.h"
#include "indexer/SymbolName.h"
#include "indexer/Tracing.h"

namespace scip {

std::strong_ordering compareRelationships(const scip::Relationship &lhs,
                                          const scip::Relationship &rhs) {
  CMP_EXPR(lhs.is_definition(), rhs.is_definition());
  CMP_EXPR(lhs.is_reference(), rhs.is_reference());
  CMP_EXPR(lhs.is_type_definition(), rhs.is_type_definition());
  CMP_EXPR(lhs.is_implementation(), rhs.is_implementation());
  CMP_STR(lhs.symbol(), rhs.symbol());
  return std::strong_ordering::equal;
}

std::strong_ordering operator<=>(const RelationshipExt &lhs,
                                 const RelationshipExt &rhs) {
  return scip::compareRelationships(lhs.rel, rhs.rel);
}

std::strong_ordering
compareScipRange(const google::protobuf::RepeatedField<int32_t> &a,
                 const google::protobuf::RepeatedField<int32_t> &b) {
  CMP_EXPR(a[0], b[0]);         // start line
  CMP_EXPR(a[1], b[1]);         // start column
  CMP_EXPR(a.size(), b.size()); // is one of these multiline
  CMP_EXPR(a[2], b[2]);         // end line or column
  if (a.size() == 3) {
    return std::strong_ordering::equal;
  }
  ENFORCE(a.size() == 4);
  CMP_EXPR(a[3], b[3]);
  return std::strong_ordering::equal;
}

std::strong_ordering compareOccurrences(const Occurrence &lhs,
                                        const Occurrence &rhs) {
  CMP_CHECK(compareScipRange(lhs.range(), rhs.range()));
  CMP_STR(lhs.symbol(), rhs.symbol());
  CMP_EXPR(lhs.symbol_roles(), rhs.symbol_roles());
  CMP_EXPR(lhs.syntax_kind(), rhs.syntax_kind());
  CMP_CHECK(cmp::compareRange(lhs.override_documentation(),
                              rhs.override_documentation(),
                              [](const auto &d1s, const auto &d2s) {
                                CMP_STR(d1s, d2s);
                                return std::strong_ordering::equal;
                              }));
  CMP_CHECK(cmp::compareRange(lhs.diagnostics(), rhs.diagnostics(),
                              [](const auto &d1, const auto &d2) {
                                CMP_EXPR(d1.severity(), d2.severity());
                                CMP_STR(d1.code(), d2.code());
                                CMP_STR(d1.message(), d2.message());
                                CMP_STR(d1.source(), d2.source());
                                return cmp::compareRange(d1.tags(), d2.tags());
                              }));
  return std::strong_ordering::equal;
}

std::strong_ordering operator<=>(const OccurrenceExt &lhs,
                                 const OccurrenceExt &rhs) {
  return scip::compareOccurrences(lhs.occ, rhs.occ);
}

bool SymbolInformationBuilder::hasDocumentation() const {
  return !this->documentation.empty()
         && this->documentation[0] != scip::missingDocumentationPlaceholder;
}

// static
bool SymbolInformationBuilder::hasDocumentation(
    const scip::SymbolInformation &symbolInfo) {
  return symbolInfo.documentation_size() > 0
         && symbolInfo.documentation()[0]
                != scip::missingDocumentationPlaceholder;
}

void SymbolInformationBuilder::finish(bool deterministic,
                                      scip::SymbolInformation &out) {
  this->_bomb.defuse();

  out.mutable_documentation()->Reserve(this->documentation.size());
  for (auto &doc : this->documentation) {
    *out.add_documentation() = std::move(doc);
  }

  out.mutable_relationships()->Reserve(this->relationships.size());
  scip_clang::extractTransform(
      std::move(this->relationships), deterministic,
      absl::FunctionRef<void(RelationshipExt &&)>([&](auto &&relExt) {
        *out.add_relationships() = std::move(relExt.rel);
      }));
}

void ForwardDeclResolver::insert(SymbolSuffix suffix,
                                 SymbolInformationBuilder *builder) {
  this->docInternalMap.emplace(suffix, SymbolInfoOrBuilderPtr{builder});
}

void ForwardDeclResolver::insert(SymbolSuffix suffix,
                                 SymbolInformation *symbolInfo) {
  this->docInternalMap.emplace(suffix, SymbolInfoOrBuilderPtr{symbolInfo});
}

void ForwardDeclResolver::insertExternal(SymbolNameRef symbol) {
  if (auto optSuffix = symbol.getPackageAgnosticSuffix()) {
    this->externalsMap[*optSuffix].insert(symbol);
  }
}

std::optional<ForwardDeclResolver::SymbolInfoOrBuilderPtr>
ForwardDeclResolver::lookupInDocuments(SymbolSuffix suffix) const {
  auto it = this->docInternalMap.find(suffix);
  if (it == this->docInternalMap.end()) {
    return {};
  }
  return it->second;
}

const absl::flat_hash_set<SymbolNameRef> *
ForwardDeclResolver::lookupExternals(SymbolSuffix suffix) const {
  auto it = this->externalsMap.find(suffix);
  if (it == this->externalsMap.end()) {
    return {};
  }
  return &it->second;
}

void ForwardDeclResolver::deleteExternals(SymbolSuffix suffix) {
  this->externalsMap.erase(suffix);
}

SymbolNameRef SymbolNameInterner::intern(std::string &&s) {
  return SymbolNameRef{std::string_view(this->impl.save(s))};
}

SymbolNameRef SymbolNameInterner::intern(SymbolNameRef s) {
  return SymbolNameRef{
      std::string_view(this->impl.save(llvm::StringRef(s.value)))};
}

DocumentBuilder::DocumentBuilder(scip::Document &&first,
                                 SymbolNameInterner interner)
    : soFar(), interner(interner),
      _bomb(BOMB_INIT(
          fmt::format("DocumentBuilder for '{}", first.relative_path()))) {
  auto &language = *first.mutable_language();
  this->soFar.set_language(std::move(language));
  auto &relativePath = *first.mutable_relative_path();
  this->soFar.set_relative_path(std::move(relativePath));
  this->merge(std::move(first));
}

void DocumentBuilder::merge(scip::Document &&doc) {
  for (auto &occ : *doc.mutable_occurrences()) {
    this->occurrences.insert({std::move(occ)});
  }
  for (auto &symbolInfo : *doc.mutable_symbols()) {
    auto name = this->interner.intern(std::move(*symbolInfo.mutable_symbol()));
    auto it = this->symbolInfos.find(name);
    if (it == this->symbolInfos.end()) {
      // SAFETY: Don't inline this initializer call since lack of
      // guarantees around subexpression evaluation order mean that
      // the std::move(name) may happen before passing name to
      // the initializer.
      SymbolInformationBuilder builder{
          name, std::move(*symbolInfo.mutable_documentation()),
          std::move(*symbolInfo.mutable_relationships())};
      this->symbolInfos.emplace(name, std::move(builder));
      continue;
    }
    auto &symbolInfoBuilder = it->second;
    if (!symbolInfoBuilder.hasDocumentation()) {
      auto &docs = *symbolInfo.mutable_documentation();
      symbolInfoBuilder.setDocumentation(std::move(docs));
    }
    symbolInfoBuilder.mergeRelationships(
        std::move(*symbolInfo.mutable_relationships()));
  }
}

void DocumentBuilder::populateForwardDeclResolver(
    ForwardDeclResolver &forwardDeclResolver) {
  for (auto &[symbolName, symbolInfoBuilder] : this->symbolInfos) {
    if (auto optSuffix = symbolName.getPackageAgnosticSuffix()) {
      forwardDeclResolver.insert(*optSuffix, &symbolInfoBuilder);
    }
  }
}

void DocumentBuilder::finish(bool deterministic, scip::Document &out) {
  this->_bomb.defuse();

  this->soFar.mutable_occurrences()->Reserve(this->occurrences.size());
  this->soFar.mutable_symbols()->Reserve(this->symbolInfos.size());

  scip_clang::extractTransform(
      std::move(this->occurrences), deterministic,
      absl::FunctionRef<void(OccurrenceExt &&)>([&](auto &&occExt) {
        *this->soFar.add_occurrences() = std::move(occExt.occ);
      }));

  scip_clang::extractTransform(
      std::move(this->symbolInfos), deterministic,
      absl::FunctionRef<void(SymbolNameRef &&, SymbolInformationBuilder &&)>(
          [&](auto &&name, auto &&builder) {
            scip::SymbolInformation symbolInfo{};
            symbolInfo.set_symbol(name.value.data(), name.value.size());
            builder.finish(deterministic, symbolInfo);
            *this->soFar.add_symbols() = std::move(symbolInfo);
          }));
  out = std::move(this->soFar);
}

RootRelativePath::RootRelativePath(std::string &&value)
    : value(std::move(value)) {
  ENFORCE(!this->value.empty());
  ENFORCE(llvm::sys::path::is_relative(this->value));
}

void ForwardDeclOccurrence::addTo(scip::Occurrence &occ) {
  auto sym = this->symbol.value;
  occ.set_symbol(sym.data(), sym.size());
  for (size_t i = 0; i < 4 && this->range[i] != -1; ++i) {
    occ.add_range(this->range[i]);
  }
}

IndexBuilder::IndexBuilder(SymbolNameInterner interner)
    : multiplyIndexed(), externalSymbols(), interner(interner),
      _bomb(BOMB_INIT("IndexBuilder")) {}

void IndexBuilder::addDocument(scip::Document &&doc, bool isMultiplyIndexed) {
  ENFORCE(!doc.relative_path().empty());
  if (isMultiplyIndexed) {
    RootRelativePath docPath{std::string(doc.relative_path())};
    auto it = this->multiplyIndexed.find(docPath);
    if (it == this->multiplyIndexed.end()) {
      this->multiplyIndexed.insert(
          {std::move(docPath),
           std::make_unique<DocumentBuilder>(std::move(doc), this->interner)});
    } else {
      auto &docBuilder = it->second;
      docBuilder->merge(std::move(doc));
    }
  } else {
    ENFORCE(!this->multiplyIndexed.contains(
                RootRelativePath{std::string(doc.relative_path())}),
            "Document with path '{}' found in multiplyIndexed map despite "
            "!isMultiplyIndexed",
            doc.relative_path());
    this->documents.emplace_back(std::move(doc));
  }
}

void IndexBuilder::addExternalSymbolUnchecked(
    SymbolNameRef name, scip::SymbolInformation &&extSym) {
  std::vector<std::string> docs{};
  absl::c_move(*extSym.mutable_documentation(), std::back_inserter(docs));
  absl::flat_hash_set<RelationshipExt> rels{};
  for (auto &rel : *extSym.mutable_relationships()) {
    rels.insert({std::move(rel)});
  }
  auto builder = std::make_unique<SymbolInformationBuilder>(
      name, std::move(docs), std::move(rels));
  this->externalSymbols.emplace(name, std::move(builder));
}

void IndexBuilder::addExternalSymbol(scip::SymbolInformation &&extSym) {
  auto name = this->interner.intern(std::move(*extSym.mutable_symbol()));
  auto it = this->externalSymbols.find(name);
  if (it == this->externalSymbols.end()) {
    this->addExternalSymbolUnchecked(name, std::move(extSym));
    return;
  }
  // NOTE(def: precondition-deterministic-ext-symbol-docs)
  // Picking the first non-empty bit of documentation will be deterministic
  // so long as external symbols are added in a deterministic order.
  auto &builder = it->second;
  if (!builder->hasDocumentation() && extSym.documentation_size() > 0) {
    builder->setDocumentation(std::move(*extSym.mutable_documentation()));
  }
  builder->mergeRelationships(std::move(*extSym.mutable_relationships()));
}

std::unique_ptr<ForwardDeclResolver>
IndexBuilder::populateForwardDeclResolver() {
  TRACE_EVENT(scip_clang::tracing::indexMerging,
              "IndexBuilder::populateForwardDeclResolver", "documents.size",
              this->documents.size(), "multiplyIndexed.size",
              this->multiplyIndexed.size());
  ForwardDeclResolver forwardDeclResolver;
  for (auto &document : this->documents) {
    for (auto &symbolInfo : *document.mutable_symbols()) {
      if (auto optSuffix =
              SymbolNameRef{symbolInfo.symbol()}.getPackageAgnosticSuffix()) {
        forwardDeclResolver.insert(*optSuffix, &symbolInfo);
      }
    }
  }
  for (auto &[_, docBuilder] : this->multiplyIndexed) {
    docBuilder->populateForwardDeclResolver(forwardDeclResolver);
  }
  for (auto &[symbolName, _] : this->externalSymbols) {
    forwardDeclResolver.insertExternal(symbolName);
  }
  return std::make_unique<ForwardDeclResolver>(std::move(forwardDeclResolver));
}

void IndexBuilder::addForwardDeclaration(
    ForwardDeclResolver &forwardDeclResolver,
    scip::ForwardDecl &&forwardDeclSym) {
  auto suffix = SymbolSuffix{
      this->interner.intern(std::move(*forwardDeclSym.mutable_suffix())).value};
  auto optSymbolInfoOrBuilderPtr =
      forwardDeclResolver.lookupInDocuments(suffix);
  if (!optSymbolInfoOrBuilderPtr.has_value()) {
    if (auto *externals = forwardDeclResolver.lookupExternals(suffix)) {
      // The main index confirms that this was an external symbol, which means
      // that we must have seen the definition in an out-of-project file.
      // In this case, just throw away the information we have,
      // and rely on what we found externally as the source of truth.
      ENFORCE(!externals->empty(),
              "externals list for a suffix should be non-empty");
      // TODO: Log a debug warning if externals->size() > 1
      for (auto symbolName : *externals) {
        auto it = this->externalSymbols.find(symbolName);
        ENFORCE(it != this->externalSymbols.end(),
                "lookupExternals succeeded earlier");
        if (!it->second->hasDocumentation()) {
          llvm::SmallVector<std::string, 1> vec{forwardDeclSym.documentation()};
          it->second->setDocumentation(std::move(vec));
        }
        this->addForwardDeclOccurrences(symbolName,
                                        scip::ForwardDecl{forwardDeclSym});
      }
    } else {
      scip::SymbolInformation extSym{};
      // Make up a fake prefix, as we have no package information 🤷🏽
      auto name = this->interner.intern(
          std::move(suffix.addFakePrefix().asStringRefMut()));
      *extSym.add_documentation() = std::move(forwardDeclSym.documentation());
      this->addExternalSymbolUnchecked(name, std::move(extSym));
      forwardDeclResolver.insertExternal(name);
      this->addForwardDeclOccurrences(name, std::move(forwardDeclSym));
    }
    return;
  }
  auto symbolInfoOrBuilderPtr = optSymbolInfoOrBuilderPtr.value();
  if (auto *externals = forwardDeclResolver.lookupExternals(suffix)) {
    // We found the symbol in a document, so the external symbols list is
    // too pessimistic. This can happen when a TU processes a decl only via
    // a forward decl (and hence conservatively assumes it must be external),
    // but another in-project TU contains the definition.
    //
    // So remove the entry from the external symbols list.
    for (auto name : *externals) {
      auto it = this->externalSymbols.find(name);
      if (it != this->externalSymbols.end()) {
        it->second->discard();
        this->externalSymbols.erase(it);
      }
    }
    forwardDeclResolver.deleteExternals(suffix);
  }
  SymbolNameRef name;
  if (auto *symbolInfo =
          symbolInfoOrBuilderPtr.dyn_cast<scip::SymbolInformation *>()) {
    name = SymbolNameRef{std::string_view(symbolInfo->symbol())};
  } else {
    auto *symbolInfoBuilder =
        symbolInfoOrBuilderPtr.get<SymbolInformationBuilder *>();
    name = symbolInfoBuilder->name;
  }
  name = this->interner.intern(name);
  if (!forwardDeclSym.documentation().empty()) {
    // FIXME(def: better-doc-merging): The missing documentation placeholder
    // value is due to a bug in the backend which seemingly came up randomly
    // and also got fixed somehow. We should remove the workaround if this
    // is no longer an issue.
    if (auto *symbolInfo =
            symbolInfoOrBuilderPtr.dyn_cast<scip::SymbolInformation *>()) {
      if (!SymbolInformationBuilder::hasDocumentation(*symbolInfo)) {
        symbolInfo->mutable_documentation()->Clear();
        *symbolInfo->add_documentation() =
            std::move(*forwardDeclSym.mutable_documentation());
      }
    } else {
      auto &symbolInfoBuilder =
          *symbolInfoOrBuilderPtr.get<SymbolInformationBuilder *>();
      // FIXME(def: better-doc-merging): We shouldn't drop
      // documentation attached to a forward declaration.
      if (!symbolInfoBuilder.hasDocumentation()) {
        llvm::SmallVector<std::string, 1> docs;
        docs.emplace_back(std::move(*forwardDeclSym.mutable_documentation()));
        symbolInfoBuilder.setDocumentation(std::move(docs));
      }
    }
  }
  this->addForwardDeclOccurrences(name, std::move(forwardDeclSym));
}

void IndexBuilder::addForwardDeclOccurrences(SymbolNameRef name,
                                             scip::ForwardDecl &&forwardDecl) {
  for (auto &ref : *forwardDecl.mutable_references()) {
    auto path =
        this->interner.intern(std::move(*ref.mutable_relative_path())).value;
    // deliberate default initialization
    this->forwardDeclOccurenceMap[path].emplace_back(
        ForwardDeclOccurrence{name, ref});
  }
}

struct IndexWriter {
  scip::Index index;
  ForwardDeclOccurrenceMap forwardDeclOccurenceMap;
  std::ostream &outputStream;

  ~IndexWriter() {
    this->write();
  }

  void writeDocument(scip::Document &&doc, bool deterministic) {
    auto path = std::string_view(doc.relative_path());
    auto it = this->forwardDeclOccurenceMap.find(path);
    if (it != this->forwardDeclOccurenceMap.end()) {
      for (auto fwdDeclOcc : it->second) {
        scip::Occurrence occ;
        fwdDeclOcc.addTo(occ);
        *doc.add_occurrences() = std::move(occ);
      }
      if (deterministic) {
        absl::c_sort(*doc.mutable_occurrences(),
                     [](const scip::Occurrence &lhs,
                        const scip::Occurrence &rhs) -> bool {
                       return scip::compareOccurrences(lhs, rhs)
                              == std::strong_ordering::less;
                     });
      }
    }
    *this->index.add_documents() = std::move(doc);
    this->write();
  }

  void writeExternalSymbol(scip::SymbolInformation &&symbolInfo) {
    *this->index.add_external_symbols() = std::move(symbolInfo);
    if (this->index.external_symbols_size() % 1024 == 0) {
      this->write();
    }
  }

private:
  void write() {
    this->index.SerializeToOstream(&this->outputStream);
    this->index.clear_documents();
    this->index.clear_external_symbols();
  }
};

void IndexBuilder::finish(bool deterministic, std::ostream &outputStream) {
  TRACE_EVENT(scip_clang::tracing::indexIo, "IndexBuilder::finish",
              "documents.size", this->documents.size(), "multiplyIndexed.size",
              this->multiplyIndexed.size(), "externalSymbols.size",
              this->externalSymbols.size());
  this->_bomb.defuse();
  IndexWriter writer{scip::Index{}, std::move(this->forwardDeclOccurenceMap),
                     outputStream};

  for (auto &doc : this->documents) {
    writer.writeDocument(std::move(doc), deterministic);
  }
  scip_clang::extractTransform(
      std::move(this->multiplyIndexed), deterministic,
      absl::FunctionRef<void(RootRelativePath &&,
                             std::unique_ptr<DocumentBuilder> &&)>(
          [&](auto && /*path*/, auto &&builder) -> void {
            scip::Document doc{};
            builder->finish(deterministic, doc);
            writer.writeDocument(std::move(doc), deterministic);
          }));
  scip_clang::extractTransform(
      std::move(this->externalSymbols), deterministic,
      absl::FunctionRef<void(SymbolNameRef &&,
                             std::unique_ptr<SymbolInformationBuilder> &&)>(
          [&](auto &&name, auto &&builder) -> void {
            scip::SymbolInformation extSym{};
            extSym.set_symbol(name.value.data(), name.value.size());
            builder->finish(deterministic, extSym);
            writer.writeExternalSymbol(std::move(extSym));
          }));
}

} // namespace scip
