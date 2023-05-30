#include <cstdlib>
#include <fstream>
#include <string_view>

#include "absl/strings/str_split.h"
#include "spdlog/spdlog.h"

#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

#include "llvm/Support/FileSystem.h"

#include "indexer/FileSystem.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/PackageMap.h"
#include "indexer/Path.h"

namespace {

struct PackageMapEntry {
  std::string rootPath;
  std::string package;
};

bool checkValid(std::string_view s, std::string_view context) {
  for (auto c : s) {
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')
        || ('0' <= c && c <= '9') || c == '.' || c == '_' || c == '-') {
      continue;
    }
    spdlog::warn(
        "invalid character '{}' in {}, expected one of [a-zA-Z0-9._\\-]", c,
        context);
    return false;
  }
  return true;
}

} // namespace

namespace llvm::json {

bool fromJSON(const llvm::json::Value &jsonValue, ::PackageMapEntry &entry,
              llvm::json::Path path) {
  llvm::json::ObjectMapper mapper(jsonValue, path);
  return mapper && mapper.map("path", entry.rootPath)
         && mapper.map("package", entry.package);
}

} // namespace llvm::json

namespace scip_clang {

PackageMap::PackageMap(const RootPath &projectRootPath,
                       const StdPath &packageMapPath, bool isTesting)
    : storage(), interner(this->storage), map(), warnedBadPaths(),
      projectRootPath(projectRootPath), isTesting(isTesting) {
  if (!packageMapPath.empty()) {
    this->populate(packageMapPath);
  }
}

void PackageMap::populate(const StdPath &packageMapPath) {
  std::error_code error;
  auto path = std::string_view(packageMapPath.c_str());
  if (!llvm::sys::fs::exists(path)) {
    spdlog::error("package map not found at path: {}", path);
    std::exit(EXIT_FAILURE);
  }
  std::ifstream in(path, std::ios_base::in | std::ios_base::binary);
  std::string contents((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());

  auto vecOrError =
      llvm::json::parse<std::vector<PackageMapEntry>>(contents, "");
  if (auto error = vecOrError.takeError()) {
    // TODO: How to get an error message out of the error?
    spdlog::error("failed to parse package map: {}", llvm_ext::format(error));
    std::exit(EXIT_FAILURE);
  }
  if (vecOrError.get().size() == 0) {
    spdlog::error(
        "package map had size 0, make sure to add one entry per package");
    std::exit(EXIT_FAILURE);
  }
  llvm::SmallString<256> buf{};
  bool foundMainPackageMetadata = false;
  for (auto &packageMapEntry : vecOrError.get()) {
    auto error = llvm::sys::fs::real_path(packageMapEntry.rootPath, buf);
    if (error) {
      spdlog::error("could not resolve path '{}' in package map: {}",
                    packageMapEntry.rootPath, error.message());
      continue;
    }
    if (!llvm::sys::fs::is_directory(buf)) {
      spdlog::warn("path '{}' in package map is not a directory (or a symlink "
                   "to a directory); skipping",
                   packageMapEntry.rootPath);
      continue;
    }
    if (buf.back() == std::filesystem::path::preferred_separator) {
      // See NOTE(ref: no-trailing-slash-for-dirs)
      buf.pop_back();
    }
    auto pathKey = this->store(std::string_view(buf.c_str(), buf.size()));
    buf.clear();
    std::vector<std::string_view> v =
        absl::StrSplit(packageMapEntry.package, '@');
    if ((v.size() != 2) || v[0].empty() || v[1].empty()) {
      spdlog::error("expected 'package' key to be in 'name@version' format, "
                    "but found '{}'",
                    packageMapEntry.package);
      continue;
    }
    auto name = this->store(v[0]);
    auto version = this->store(v[1]);
    if (!::checkValid(name, "name") || !::checkValid(version, "version")) {
      continue;
    }
    auto rootPath = AbsolutePathRef::tryFrom(pathKey).value();
    bool isMainPackage = rootPath == this->projectRootPath.asRef();
    auto [it, inserted] = this->map.emplace(
        rootPath, PackageMetadata{{name, version}, rootPath, isMainPackage});
    if (inserted) {
      foundMainPackageMetadata |= isMainPackage;
    } else {
      auto prior = it->second.id;
      if (prior.name != name || prior.version != version) {
        spdlog::warn("package map has conflicting package information ('{}@{}' "
                     "and '{}@{}') for the same path '{}'",
                     name, version, prior.name, prior.version, pathKey);
      }
    }
  }
  if (!foundMainPackageMetadata) {
    spdlog::error(
        "missing package information for the current project in package map");
    spdlog::info(
        R"(hint: add an object with {{"path": ".", "package": "blah@vX.Y"}} or)"
        R"( {{"path": "{}", "package": "blah@vX.Y"}} to {})",
        this->projectRootPath.asRef().asStringView(), packageMapPath.string());
    std::exit(EXIT_FAILURE);
  }
}

std::string_view PackageMap::store(std::string_view p) {
  return this->interner.save(llvm::StringRef(p.data(), p.size()));
}

bool PackageMap::checkPathIsNormalized(AbsolutePathRef filepath) {
  if (filepath.isNormalized()) {
    return true;
  }
  // Limit the number of warnings to avoid log spew
  if (this->warnedBadPaths.size() < 5
      && !this->warnedBadPaths.contains(filepath.asStringView())) {
    auto s = this->store(filepath.asStringView());
    this->warnedBadPaths.insert(s);
    spdlog::warn("unexpected non-normalized path '{}' when looking up package "
                 "information; please report this as a scip-clang bug",
                 s);
  }
  return false;
}

static PackageId testPackageId = PackageId{"test-pkg", "test-version"};

std::optional<PackageMetadata> PackageMap::lookup(AbsolutePathRef filepath) {
  if (this->map.empty()) {
    if (this->isTesting) {
      return PackageMetadata{
          testPackageId,
          AbsolutePathRef::tryFrom(std::string_view("/")).value(),
          /*isMainPackage*/ true};
    }
    return {};
  }
  auto it = this->map.find(filepath);
  if (it != this->map.end()) {
    return it->second;
  }
  llvm::SmallString<64> buf;
  // A PackageMap persists across TUs, whereas the filepath argument
  // is a reference that lives only as long as a TU, so intern the path
  // before adding extra references inside the map later.
  if (this->checkPathIsNormalized(filepath)) {
    filepath =
        AbsolutePathRef::tryFrom(this->store(filepath.asStringView())).value();
  } else {
    filepath.normalize(buf);
    filepath = AbsolutePathRef::tryFrom(this->store(buf.str())).value();
  }
  llvm::SmallVector<AbsolutePathRef, 4> prefixLookup;
  auto prefixesBegin = filepath.prefixesBegin();
  auto prefixesEnd = filepath.prefixesEnd();
  for (auto prefix = prefixesBegin; prefix != prefixesEnd; ++prefix) {
    auto it = this->map.find(*prefix);
    if (it == this->map.end()) {
      prefixLookup.push_back(*prefix);
      continue;
    }
    auto packageInfo = it->second;
    for (auto p : prefixLookup) {
      this->map.insert({p, packageInfo});
    }
    return packageInfo;
  }
  return {};
}

} // namespace scip_clang