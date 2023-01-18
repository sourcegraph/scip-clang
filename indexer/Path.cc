#include <optional>
#include <string_view>
#include <type_traits>

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"

#include "indexer/LLVMAdapter.h"
#include "indexer/Path.h"

namespace scip_clang {

std::optional<AbsolutePathRef> AbsolutePathRef::tryFrom(std::string_view path) {
  if (llvm::sys::path::is_absolute(llvm::Twine(path))) {
    return {AbsolutePathRef(path)};
  }
  return {};
}

std::optional<std::string_view>
AbsolutePathRef::makeRelative(AbsolutePathRef longerPath) {
  if (longerPath._data.starts_with(this->_data)) {
    auto start = this->_data.size();
    while (start < longerPath._data.size() && longerPath._data[start] == '/') {
      ++start;
    }
    return longerPath._data.substr(start);
  }
  return {};
}

const std::string_view Path::filename() const {
  ENFORCE(llvm::sys::path::has_filename(this->_data));
  return scip_clang::toStringView(llvm::sys::path::filename(this->_data));
}

static_assert(std::is_swappable<Path>::value, "Needed for sorting");

} // namespace scip_clang