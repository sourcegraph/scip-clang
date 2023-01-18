#include <compare>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/function_ref.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/Comparison.h"
#include "indexer/Enforce.h"
#include "indexer/ScipExtras.h"

namespace scip {

std::strong_ordering operator<=>(const RelationshipExt &lhs,
                                 const RelationshipExt &rhs) {
  CMP_EXPR(lhs.rel.is_definition(), rhs.rel.is_definition());
  CMP_EXPR(lhs.rel.is_reference(), rhs.rel.is_reference());
  CMP_EXPR(lhs.rel.is_type_definition(), rhs.rel.is_type_definition());
  CMP_EXPR(lhs.rel.is_implementation(), rhs.rel.is_implementation());
  CMP_STR(lhs.rel.symbol(), rhs.rel.symbol());
  return std::strong_ordering::equal;
}

std::strong_ordering operator<=>(const OccurrenceExt &lhs,
                                 const OccurrenceExt &rhs) {
  CMP_EXPR(lhs.occ.symbol_roles(), rhs.occ.symbol_roles());
  CMP_STR(lhs.occ.symbol(), rhs.occ.symbol());
  CMP_RANGE(lhs.occ.range(), rhs.occ.range());
  CMP_EXPR(lhs.occ.syntax_kind(), rhs.occ.syntax_kind());
  CMP_CHECK(cmp::compareRange(lhs.occ.override_documentation(),
                              rhs.occ.override_documentation(),
                              [](const auto &d1s, const auto &d2s) {
                                CMP_STR(d1s, d2s);
                                return std::strong_ordering::equal;
                              }));
  CMP_CHECK(cmp::compareRange(lhs.occ.diagnostics(), rhs.occ.diagnostics(),
                              [](const auto &d1, const auto &d2) {
                                CMP_EXPR(d1.severity(), d2.severity());
                                CMP_STR(d1.code(), d2.code());
                                CMP_STR(d1.message(), d2.message());
                                CMP_STR(d1.source(), d2.source());
                                return cmp::compareRange(d1.tags(), d2.tags());
                              }));
  return std::strong_ordering::equal;
}

void SymbolInformationBuilder::finish(bool deterministic,
                                      scip::SymbolInformation &out) {
  for (auto &doc : this->documentation) {
    *out.add_documentation() = std::move(doc);
  }
  if (!deterministic) {
    for (auto &relExt : this->relationships) {
      *out.add_relationships() = std::move(relExt.rel);
    }
  } else {
    std::vector<RelationshipExt> rels{};
    absl::c_move(this->relationships, std::back_inserter(rels));
    absl::c_sort(rels);
    for (auto &relExt : rels) {
      *out.add_relationships() = std::move(relExt.rel);
    }
  }
}

DocumentBuilder::DocumentBuilder(scip::Document &&first) : soFar() {
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
  for (auto &rel : *doc.mutable_symbols()) {
    auto &name = *rel.mutable_symbol();
    auto it = this->symbolInfos.find(name);
    if (it == this->symbolInfos.end()) {
      this->symbolInfos.insert(
          {std::move(name),
           SymbolInformationBuilder{std::move(*rel.mutable_documentation()),
                                    std::move(*rel.mutable_relationships())}});
      continue;
    }
    if (!it->second.hasDocumentation()) {
      auto &docs = *rel.mutable_documentation();
      it->second.setDocumentation(std::move(docs));
    }
    it->second.mergeRelationships(std::move(*rel.mutable_relationships()));
  }
}

void DocumentBuilder::finish(bool deterministic, scip::Document &out) {
  this->soFar.mutable_occurrences()->Reserve(this->occurrences.size());
  this->soFar.mutable_symbols()->Reserve(this->symbolInfos.size());

  scip_clang::extractTransform(
      std::move(this->occurrences), deterministic,
      absl::FunctionRef<void(OccurrenceExt &&)>([&](auto &&occExt) {
        *this->soFar.add_occurrences() = std::move(occExt.occ);
      }));

  scip_clang::extractTransform(
      std::move(this->symbolInfos), deterministic,
      absl::FunctionRef<void(std::string &&, SymbolInformationBuilder &&)>(
          [&](auto &&name, auto &&builder) {
            scip::SymbolInformation symbolInfo{};
            symbolInfo.set_symbol(name);
            builder.finish(deterministic, symbolInfo);
            *this->soFar.add_symbols() = std::move(symbolInfo);
          }));
  out = std::move(this->soFar);
}

IndexBuilder::IndexBuilder(scip::Index &fullIndex)
    : fullIndex(fullIndex), multiplyIndexed(), externalSymbols(), _bomb() {}

void IndexBuilder::addDocument(scip::Document &&doc, bool isMultiplyIndexed) {
  auto &docPath = doc.relative_path();
  if (isMultiplyIndexed) {
    auto it = this->multiplyIndexed.find(docPath);
    if (it == this->multiplyIndexed.end()) {
      this->multiplyIndexed.insert(
          {docPath, std::make_unique<DocumentBuilder>(std::move(doc))});
    } else {
      it->second->merge(std::move(doc));
    }
  } else {
    ENFORCE(!this->multiplyIndexed.contains(doc.relative_path()),
            "Document with path '{}' found in multiplyIndexed map despite "
            "!isMultiplyIndexed",
            doc.relative_path());
    *this->fullIndex.add_documents() = std::move(doc);
  }
}

void IndexBuilder::addExternalSymbol(scip::SymbolInformation &&extSym) {
  auto &name = extSym.symbol();
  auto it = this->externalSymbols.find(name);
  if (it == this->externalSymbols.end()) {
    std::vector<std::string> docs{};
    absl::c_move(*extSym.mutable_documentation(), std::back_inserter(docs));
    absl::flat_hash_set<RelationshipExt> rels{};
    for (auto &rel : *extSym.mutable_relationships()) {
      rels.insert({std::move(rel)});
    }
    this->externalSymbols.insert(
        {name, std::make_unique<SymbolInformationBuilder>(std::move(docs),
                                                          std::move(rels))});
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

void IndexBuilder::finish(bool deterministic) {
  this->_bomb.defuse();

  this->fullIndex.mutable_documents()->Reserve(this->multiplyIndexed.size());
  this->fullIndex.mutable_external_symbols()->Reserve(
      this->externalSymbols.size());

  scip_clang::extractTransform(
      std::move(this->multiplyIndexed), deterministic,
      absl::FunctionRef<void(std::string &&,
                             std::unique_ptr<DocumentBuilder> &&)>(
          [&](auto && /*path*/, auto &&builder) -> void {
            scip::Document doc{};
            builder->finish(deterministic, doc);
            *this->fullIndex.add_documents() = std::move(doc);
          }));

  scip_clang::extractTransform(
      std::move(this->externalSymbols), deterministic,
      absl::FunctionRef<void(std::string &&,
                             std::unique_ptr<SymbolInformationBuilder> &&)>(
          [&](auto &&name, auto &&builder) -> void {
            scip::SymbolInformation extSym;
            extSym.set_symbol(std::move(name));
            builder->finish(deterministic, extSym);
            *this->fullIndex.add_external_symbols() = std::move(extSym);
          }));
}

} // namespace scip
