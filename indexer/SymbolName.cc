#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "indexer/SymbolFormatter.h"
#include "indexer/SymbolName.h"

namespace scip {

std::optional<SymbolSuffix> SymbolNameRef::getPackageAgnosticSuffix() const {
  return scip_clang::SymbolBuilder::getPackageAgnosticSuffix(*this);
}

SymbolName SymbolSuffix::addFakePrefix() const {
  return scip_clang::SymbolBuilder::addFakePrefix(*this);
}

} // namespace scip

namespace scip_clang {

// static
std::optional<scip::SymbolSuffix>
SymbolBuilder::getPackageAgnosticSuffix(scip::SymbolNameRef name) {
  // See NOTE(ref: symbol-string-hack-for-forward-decls)
  auto ix = name.value.find('$');
  if (ix == std::string::npos || ix == name.value.size() - 1
      || name.value[ix + 1] != ' ') {
    return std::nullopt;
  }
  return scip::SymbolSuffix{name.value.substr(ix + 2)};
}

// static
scip::SymbolName SymbolBuilder::addFakePrefix(scip::SymbolSuffix suffix) {
  std::string buf;
#define FAKE_SYMBOL_PREFIX "cxx . . $ "
  buf.reserve(sizeof(FAKE_SYMBOL_PREFIX) + suffix.value.size());
  buf.append(FAKE_SYMBOL_PREFIX);
#undef FAKE_SYMBOL_PREFIX
  buf.append(suffix.value);
  return scip::SymbolName{std::move(buf)};
}

} // namespace scip_clang
