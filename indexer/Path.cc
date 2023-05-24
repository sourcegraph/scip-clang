#include <compare>
#include <filesystem>
#include <optional>
#include <string_view>
#include <type_traits>

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"

#include "indexer/Comparison.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"

static std::optional<std::string_view> fileName(std::string_view path) {
  auto i = path.find_last_of(std::filesystem::path::preferred_separator);
  if (i == std::string::npos || i == path.size() - 1) {
    return {};
  }
  return path.substr(i + 1);
}

namespace scip_clang {

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

std::optional<std::string_view> AbsolutePathRef::fileName() const {
  return ::fileName(this->asStringView());
}

bool AbsolutePathRef::isNormalized() const {
  auto start = llvm::sys::path::begin(this->value);
  auto end = llvm::sys::path::end(this->value);
  for (auto it = start; it != end; ++it) {
    if (it->equals(".") || it->equals("..")) {
      return false;
    }
  }
  if (this->value.find("//") != std::string::npos) {
    return false;
  }

#ifdef _WIN32
  auto ix = this->value.find_last_of("\\");
  // Absolute paths on Windows can begin with double backslash
  if (ix != std::string::npos && ix != 0) {
    return false;
  }
#endif

  return true;
}

void AbsolutePathRef::normalize(llvm::SmallVectorImpl<char> &newStorage) const {
  newStorage.clear();
  newStorage.append(this->asStringView().begin(), this->asStringView().end());
  llvm::sys::path::remove_dots(newStorage);
}

std::optional<AbsolutePathRef> AbsolutePathRef::prefix() const {
  // NOTE(def: no-trailing-slash-for-dirs): The parent_path function omits
  // any trailing separators for directories, so we should make sure that other
  // places relying on path matching do the same.
  return AbsolutePathRef::tryFrom(llvm::sys::path::parent_path(this->value));
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

RootRelativePathRef::RootRelativePathRef(std::string_view value, RootKind kind)
    : value(value), _kind(kind) {
  ENFORCE(!this->value.empty(),
          "use default ctor to make empty paths for explicitness");
  ENFORCE(llvm::sys::path::is_relative(this->value));
}

std::string_view RootRelativePathRef::extension() const {
  return llvm_ext::toStringView(
      llvm::sys::path::extension(this->asStringView()));
}

std::strong_ordering operator<=>(const RootRelativePathRef &lhs,
                                 const RootRelativePathRef &rhs) {
  CMP_STR(lhs.asStringView(), rhs.asStringView());
  CMP_EXPR(lhs._kind, rhs._kind);
  return std::strong_ordering::equal;
}

std::optional<std::string_view> RootRelativePathRef::fileName() const {
  return ::fileName(this->asStringView());
}

RootRelativePath::RootRelativePath(RootRelativePathRef ref)
    : value(ref.asStringView()), _kind(ref.kind()) {
  if (this->value.empty()) {
    return;
  }
  ENFORCE(llvm::sys::path::is_relative(this->value));
}

RootRelativePath::RootRelativePath(std::string &&path, RootKind kind)
    : value(std::move(path)), _kind(kind) {
  if (this->value.empty()) {
    return;
  }
  ENFORCE(llvm::sys::path::is_relative(this->value));
}

void RootRelativePath::replaceExtension(std::string_view newExtension) {
  auto it = this->value.rfind('.');
  if (it == std::string::npos) {
    return;
  }
  this->value.resize(it);
  this->value.append(newExtension);
}

std::optional<RootRelativePathRef>
RootPath::tryMakeRelative(AbsolutePathRef maybePathInsideProject) const {
  if (auto optStrView =
          this->value.asRef().makeRelative(maybePathInsideProject)) {
    return RootRelativePathRef(optStrView.value(), this->kind());
  }
  return std::nullopt;
}

AbsolutePath RootPath::makeAbsolute(RootRelativePathRef relativePathRef) const {
  ENFORCE(this->kind() == relativePathRef.kind())
  return this->makeAbsoluteAllowKindMismatch(relativePathRef);
}

AbsolutePath RootPath::makeAbsoluteAllowKindMismatch(
    RootRelativePathRef relativePathRef) const {
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
