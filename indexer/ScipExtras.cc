#include <compare>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/function_ref.h"

#include "llvm/Support/Path.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/Comparison.h"
#include "indexer/Enforce.h"
#include "indexer/ScipExtras.h"

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

DocumentBuilder::DocumentBuilder(scip::Document &&first)
    : soFar(), _bomb(BOMB_INIT(fmt::format("DocumentBuilder for '{}",
                                           first.relative_path()))) {
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
    SymbolName name{std::move(*symbolInfo.mutable_symbol())};
    auto it = this->symbolInfos.find(name);
    if (it == this->symbolInfos.end()) {
      // SAFETY: Don't inline this initializer call since lack of
      // guarantees around subexpression evaluation order mean that
      // the std::move(name) may happen before passing name to
      // the initializer.
      SymbolInformationBuilder builder{
          name.asStringRef(), std::move(*symbolInfo.mutable_documentation()),
          std::move(*symbolInfo.mutable_relationships())};
      this->symbolInfos.emplace(std::move(name), std::move(builder));
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

void DocumentBuilder::populateSymbolToInfoMap(
    SymbolToInfoMap &symbolToInfoMap) {
  for (auto &[symbolName, symbolInfoBuilder] : this->symbolInfos) {
    symbolToInfoMap.emplace(std::string_view(symbolName.asStringRef()),
                            SymbolToInfoMap::mapped_type(&symbolInfoBuilder));
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
      absl::FunctionRef<void(SymbolName &&, SymbolInformationBuilder &&)>(
          [&](auto &&name, auto &&builder) {
            scip::SymbolInformation symbolInfo{};
            symbolInfo.set_symbol(std::move(name.asStringRefMut()));
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

IndexBuilder::IndexBuilder()
    : multiplyIndexed(), externalSymbols(), _bomb(BOMB_INIT("IndexBuilder")) {}

void IndexBuilder::addDocument(scip::Document &&doc, bool isMultiplyIndexed) {
  ENFORCE(!doc.relative_path().empty());
  if (isMultiplyIndexed) {
    RootRelativePath docPath{std::string(doc.relative_path())};
    auto it = this->multiplyIndexed.find(docPath);
    if (it == this->multiplyIndexed.end()) {
      this->multiplyIndexed.insert(
          {std::move(docPath),
           std::make_unique<DocumentBuilder>(std::move(doc))});
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
    SymbolName &&name, scip::SymbolInformation &&extSym) {
  std::vector<std::string> docs{};
  absl::c_move(*extSym.mutable_documentation(), std::back_inserter(docs));
  absl::flat_hash_set<RelationshipExt> rels{};
  for (auto &rel : *extSym.mutable_relationships()) {
    rels.insert({std::move(rel)});
  }
  // SAFETY: Don't inline this assignment statement since lack of
  // guarantees around subexpression evaluation order mean that
  // the std::move(name) may happen before name.asStringRef() is called.
  auto builder = std::make_unique<SymbolInformationBuilder>(
      name.asStringRef(), std::move(docs), std::move(rels));
  this->externalSymbols.emplace(std::move(name), std::move(builder));
  return;
}

void IndexBuilder::addExternalSymbol(scip::SymbolInformation &&extSym) {
  SymbolName name{std::move(*extSym.mutable_symbol())};
  auto it = this->externalSymbols.find(name);
  if (it == this->externalSymbols.end()) {
    this->addExternalSymbolUnchecked(std::move(name), std::move(extSym));
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

std::unique_ptr<SymbolToInfoMap> IndexBuilder::populateSymbolToInfoMap() {
  SymbolToInfoMap symbolToInfoMap;
  for (auto &document : this->documents) {
    for (auto &symbolInfo : *document.mutable_symbols()) {
      auto symbolName = std::string_view(symbolInfo.symbol());
      symbolToInfoMap.emplace(symbolName,
                              SymbolToInfoMap::mapped_type(&symbolInfo));
    }
  }
  for (auto &[_, docBuilder] : this->multiplyIndexed) {
    docBuilder->populateSymbolToInfoMap(symbolToInfoMap);
  }
  return std::make_unique<SymbolToInfoMap>(std::move(symbolToInfoMap));
}

void IndexBuilder::addForwardDeclaration(
    const SymbolToInfoMap &symbolToInfoMap,
    scip::SymbolInformation &&forwardDeclSym) {
  SymbolName name{std::move(*forwardDeclSym.mutable_symbol())};
  auto it = symbolToInfoMap.find(name.asStringRef());
  if (it == symbolToInfoMap.end()) {
    if (!this->externalSymbols.contains(name)) {
      this->addExternalSymbolUnchecked(std::move(name),
                                       std::move(forwardDeclSym));
    } else {
      // The main index confirms that this was an external symbol, which means
      // that we must have seen the definition in an out-of-project file.
      // In this case, just throw away the information we have,
      // and rely on what we found externally as the source of truth.
    }
    return;
  }
  auto extIt = this->externalSymbols.find(name);
  if (extIt != this->externalSymbols.end()) {
    extIt->second->discard();
    this->externalSymbols.erase(extIt);
  }
  if (!forwardDeclSym.documentation().empty()) {
    // FIXME(def: better-doc-merging): We shouldn't drop documentation
    // attached to a definition, if present.
    if (auto *symbolInfo = it->second.dyn_cast<scip::SymbolInformation *>()) {
      symbolInfo->mutable_documentation()->Clear();
      for (auto &doc : *forwardDeclSym.mutable_documentation()) {
        *symbolInfo->add_documentation() = std::move(doc);
      }
    } else {
      auto &symbolInfoBuilder = *it->second.get<SymbolInformationBuilder *>();
      // FIXME(def: better-documentation-merging): We shouldn't drop
      // documentation attached to a forward declaration.
      if (!symbolInfoBuilder.hasDocumentation()) {
        symbolInfoBuilder.setDocumentation(
            std::move(*forwardDeclSym.mutable_documentation()));
      }
    }
  }
}

struct IndexWriter {
  scip::Index index;
  std::ostream &outputStream;

  ~IndexWriter() {
    this->write();
  }

  void writeDocument(scip::Document &&doc) {
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
  this->_bomb.defuse();
  IndexWriter writer{scip::Index{}, outputStream};

  for (auto &doc : this->documents) {
    writer.writeDocument(std::move(doc));
  }
  scip_clang::extractTransform(
      std::move(this->multiplyIndexed), deterministic,
      absl::FunctionRef<void(RootRelativePath &&,
                             std::unique_ptr<DocumentBuilder> &&)>(
          [&](auto && /*path*/, auto &&builder) -> void {
            scip::Document doc{};
            builder->finish(deterministic, doc);
            writer.writeDocument(std::move(doc));
          }));
  scip_clang::extractTransform(
      std::move(this->externalSymbols), deterministic,
      absl::FunctionRef<void(SymbolName &&,
                             std::unique_ptr<SymbolInformationBuilder> &&)>(
          [&](auto &&name, auto &&builder) -> void {
            scip::SymbolInformation extSym{};
            extSym.set_symbol(std::move(name.asStringRefMut()));
            builder->finish(deterministic, extSym);
            writer.writeExternalSymbol(std::move(extSym));
          }));
}

} // namespace scip
