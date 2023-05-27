#ifndef SCIP_CLANG_SYMBOL_NAME_H
#define SCIP_CLANG_SYMBOL_NAME_H

#include <string>
#include <string_view>
#include <utility>

#include "indexer/Comparison.h"
#include "indexer/Derive.h"
#include "indexer/Enforce.h"

namespace scip {

/// An owned symbol name value
class SymbolName {
  std::string value;

  // The implicitly synthesized copy constructor is important as this is
  // used a map key, which are required to be copy-constructible.
public:
  SymbolName(std::string &&value) : value(std::move(value)) {
    ENFORCE(!this->value.empty());
  }
  const std::string &asStringRef() const {
    return this->value;
  }
  std::string &asStringRefMut() {
    return this->value;
  }
  DERIVE_HASH_CMP_NEWTYPE(SymbolName, value, CMP_STR)
};

struct SymbolSuffix {
  std::string_view value;

  DERIVE_HASH_CMP_NEWTYPE(SymbolSuffix, value, CMP_STR)

  SymbolName addFakePrefix() const;
};

/// An unowned symbol name
struct SymbolNameRef {
  std::string_view value;

  DERIVE_HASH_CMP_NEWTYPE(SymbolNameRef, value, CMP_STR)

  std::optional<SymbolSuffix> getPackageAgnosticSuffix() const;
};

} // namespace scip

namespace scip_clang {

using scip::SymbolNameRef;

} // namespace scip_clang

#endif // SCIP_CLANG_SYMBOL_NAME_H