#include <optional>
#include <string>
#include <string_view>

#include "llvm/Support/Path.h"

#include "indexer/Path.h"

namespace scip_clang {

std::optional<AbsolutePath> AbsolutePath::tryFrom(std::string_view path) {
  if (llvm::sys::path::is_absolute(llvm::Twine(path))) {
    return {AbsolutePath(path)};
  }
  return {};
}

} // namespace scip_clang