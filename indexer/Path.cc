#include <filesystem>
#include <optional>
#include <string_view>
#include <type_traits>

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"

#include "indexer/LLVMAdapter.h"
#include "indexer/Path.h"

namespace scip_clang {

ProjectRootRelativePathRef::ProjectRootRelativePathRef(std::string_view value)
    : value(value) {
  ENFORCE(!this->value.empty(),
          "use default ctor to make empty paths for explicitness");
  ENFORCE(llvm::sys::path::is_relative(this->value));
}

AbsolutePathRef::AbsolutePathRef(std::string_view value) : value(value) {
  ENFORCE(!this->value.empty());
  ENFORCE(llvm::sys::path::is_absolute(this->value));
}

std::optional<AbsolutePathRef> AbsolutePathRef::tryFrom(std::string_view path) {
  if (llvm::sys::path::is_absolute(llvm::Twine(path))) {
    return {AbsolutePathRef(path)};
  }
  return {};
}

std::optional<AbsolutePathRef> AbsolutePathRef::tryFrom(llvm::StringRef path) {
  return AbsolutePathRef::tryFrom(std::string_view(path.data(), path.size()));
}

std::optional<std::string_view>
AbsolutePathRef::makeRelative(AbsolutePathRef longerPath) {
  if (longerPath.value.starts_with(this->value)) {
    auto start = this->value.size();
    while (start < longerPath.value.size()
           && longerPath.value[start]
                  == std::filesystem::path::preferred_separator) {
      ++start;
    }
    return longerPath.value.substr(start);
  }
  return {};
}

AbsolutePathRef AbsolutePath::asRef() const {
  auto sv = std::string_view(this->value.data(), this->value.size());
  auto optRef = AbsolutePathRef::tryFrom(sv);
  ENFORCE(optRef.has_value());
  return optRef.value();
}

llvm::json::Value AbsolutePath::toJSON(const AbsolutePath &p) {
  return llvm::json::Value(p.asStringRef());
}
llvm::json::Value toJSON(const AbsolutePath &p) {
  return AbsolutePath::toJSON(p);
}

bool AbsolutePath::fromJSON(const llvm::json::Value &value, AbsolutePath &p,
                            llvm::json::Path path) {
  return llvm::json::fromJSON(value, p.value, path);
}
bool fromJSON(const llvm::json::Value &value, AbsolutePath &p,
              llvm::json::Path path) {
  return AbsolutePath::fromJSON(value, p, path);
}

std::optional<ProjectRootRelativePathRef>
ProjectRootPath::tryMakeRelative(AbsolutePathRef maybePathInsideProject) const {
  if (auto optStrView =
          this->value.asRef().makeRelative(maybePathInsideProject)) {
    return ProjectRootRelativePathRef(optStrView.value());
  }
  return std::nullopt;
}

AbsolutePath ProjectRootPath::makeAbsolute(
    ProjectRootRelativePathRef relativePathRef) const {
  std::string buf{};
  auto &absPath = this->value.asStringRef();
  auto relPath = relativePathRef.asStringView();
  auto nativeSeparator = std::filesystem::path::preferred_separator;
  if (absPath.ends_with(nativeSeparator)) {
    return AbsolutePath(fmt::format("{}{}", absPath, relPath));
  }
  return AbsolutePath(fmt::format("{}{}{}", absPath, nativeSeparator, relPath));
}

} // namespace scip_clang