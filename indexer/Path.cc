#include <optional>
#include <string>
#include <string_view>

#include "llvm/Support/Path.h"

#include "indexer/Path.h"

namespace scip_clang {

std::optional<AbsolutePathRef> AbsolutePathRef::tryFrom(std::string_view path) {
  if (llvm::sys::path::is_absolute(llvm::Twine(path))) {
    return {AbsolutePathRef(path)};
  }
  return {};
}

} // namespace scip_clang