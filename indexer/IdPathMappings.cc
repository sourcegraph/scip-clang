#include <filesystem>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"

#include "indexer/Hash.h"
#include "indexer/IdPathMappings.h"
#include "indexer/Path.h"

namespace scip_clang {

void ClangIdLookupMap::insert(AbsolutePathRef absPathRef, HashValue hashValue,
                              clang::FileID fileId) {
  auto it = this->impl.find(absPathRef);
  if (it == this->impl.end()) {
    this->impl.emplace(absPathRef, std::make_shared<Value>(Value{
                                       .hashToFileId = {{hashValue, fileId}}}));
    return;
  }
  // A single representative FileID is sufficient.
  it->second->hashToFileId[hashValue] = fileId;
}

void ClangIdLookupMap::forEachPathAndHash(
    absl::FunctionRef<void(
        AbsolutePathRef, const absl::flat_hash_map<HashValue, clang::FileID> &)>
        callback) const {
  for (auto &[absPathRef, valuePtr] : this->impl) {
    ENFORCE(!valuePtr->hashToFileId.empty(),
            "Shouldn't have stored empty maps");
    callback(absPathRef, valuePtr->hashToFileId);
  }
}

std::optional<clang::FileID>
ClangIdLookupMap::lookup(AbsolutePathRef absPathRef,
                         HashValue hashValue) const {
  auto it = this->impl.find(absPathRef);
  if (it == this->impl.end()) {
    return {};
  }
  auto hashIt = it->second->hashToFileId.find(hashValue);
  if (hashIt == it->second->hashToFileId.end()) {
    return {};
  }
  return hashIt->second;
}

std::optional<clang::FileID>
ClangIdLookupMap::lookupAnyFileId(AbsolutePathRef absPathRef) const {
  auto it = this->impl.find(absPathRef);
  if (it == this->impl.end()) {
    return {};
  }
  for (auto [hashValue, fileId] : it->second->hashToFileId) {
    return fileId;
  }
  ENFORCE(false, "Shouldn't have stored empty maps");
  return {};
}

void StableFileIdMap::populate(const ClangIdLookupMap &clangIdLookupMap) {
  clangIdLookupMap.forEachPathAndHash( // force formatting break
      [&](AbsolutePathRef absPathRef,
          const absl::flat_hash_map<HashValue, clang::FileID> &map) {
        for (auto &[hash, fileId] : map) {
          bool inserted = this->insert(fileId, absPathRef);
          ENFORCE(inserted,
                  "there is a 1-1 mapping from FileID -> (path, hash) so the"
                  " FileID {} for {} should not have been inserted earlier",
                  fileId.getHashValue(), absPathRef.asStringView());
        }
      });
}

bool StableFileIdMap::insert(clang::FileID fileId, AbsolutePathRef absPathRef) {
  ENFORCE(fileId.isValid(),
          "invalid FileIDs should be filtered out after preprocessing");
  ENFORCE(!absPathRef.asStringView().empty(),
          "inserting file with empty absolute path");

  auto insertRelPath = [&](RootRelativePathRef projectRootRelPath) -> bool {
    ENFORCE(!projectRootRelPath.asStringView().empty(),
            "file path is unexpectedly equal to project root");
    return this->map.insert({{fileId}, projectRootRelPath}).second;
  };

  // In practice, CMake ends up passing paths to project files as well
  // as files inside the build root. Normally, files inside the build root
  // are generated ones, but to be safe, check if the corresponding file
  // exists in the project. Since the build root itself is typically inside
  // the project root, check the build root first.
  if (auto buildRootRelPath = this->buildRootPath.tryMakeRelative(absPathRef)) {
    auto originalFileSourcePath =
        this->projectRootPath.makeAbsoluteAllowKindMismatch(
            buildRootRelPath.value());
    std::error_code error{};
    if (std::filesystem::exists(originalFileSourcePath.asStringRef(), error)
        && !error) {
      return insertRelPath(RootRelativePathRef(buildRootRelPath->asStringView(),
                                               RootKind::Project));
    }
  } else if (auto optProjectRootRelPath =
                 this->projectRootPath.tryMakeRelative(absPathRef)) {
    return insertRelPath(optProjectRootRelPath.value());
  }
  auto optFileName = absPathRef.fileName();
  ENFORCE(optFileName.has_value(),
          "Clang returned file path {} without a file name",
          absPathRef.asStringView());
  this->storage.emplace_back(
      fmt::format("<external>/{}/{}",
                  HashValue::forText(absPathRef.asStringView()), *optFileName),
      RootKind::Build); // fake value to satisfy the RootRelativePathRef API
  return this->map
      .insert({{fileId},
               ExternalFileEntry{absPathRef, this->storage.back().asRef()}})
      .second;
}

void StableFileIdMap::forEachFileId(
    absl::FunctionRef<void(clang::FileID, StableFileId)> callback) {
  for (auto &[wrappedFileId, entry] : this->map) {
    callback(wrappedFileId.data, Self::mapValueToStableFileId(entry));
  }
}

std::optional<StableFileId>
StableFileIdMap::getStableFileId(clang::FileID fileId) const {
  auto it = this->map.find({fileId});
  if (it == this->map.end()) {
    return {};
  }
  return Self::mapValueToStableFileId(it->second);
}

// static
StableFileId
StableFileIdMap::mapValueToStableFileId(const MapValueType &variant) {
  if (std::holds_alternative<RootRelativePathRef>(variant)) {
    return StableFileId{.path = std::get<RootRelativePathRef>(variant),
                        .isInProject = true,
                        .isSynthetic = false};
  }
  return StableFileId{.path =
                          std::get<ExternalFileEntry>(variant).fakeRelativePath,
                      .isInProject = false,
                      .isSynthetic = true};
}

} // namespace scip_clang