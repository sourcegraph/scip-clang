#include <filesystem>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/FileSystem.h"

#include "indexer/FileMetadata.h"
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

void FileMetadataMap::populate(const ClangIdLookupMap &clangIdLookupMap) {
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

bool FileMetadataMap::insert(clang::FileID fileId, AbsolutePathRef absPathRef) {
  ENFORCE(fileId.isValid(),
          "invalid FileIDs should be filtered out after preprocessing");
  ENFORCE(!absPathRef.asStringView().empty(),
          "inserting file with empty absolute path");

  auto optPackageMetadata = this->packageMap.lookup(absPathRef);

  auto insertRelPath = [&](RootRelativePathRef relPathRef,
                           bool isInProject) -> bool {
    ENFORCE(!relPathRef.asStringView().empty(),
            "file path is unexpectedly equal to project root");
    auto metadata = FileMetadata{
        StableFileId{relPathRef, isInProject, /*isSynthetic*/ false},
        absPathRef,
        optPackageMetadata,
    };
    return this->map.insert({{fileId}, std::move(metadata)}).second;
  };

  if (optPackageMetadata.has_value()) {
    if (auto optStrView =
            optPackageMetadata->rootPath.makeRelative(absPathRef)) {
      return insertRelPath(RootRelativePathRef(*optStrView, RootKind::External),
                           /*isInProject*/ optPackageMetadata->isMainPackage);
    } else {
      spdlog::warn("package info map determined '{}' as root for path '{}', "
                   "but prefix check failed",
                   optPackageMetadata->rootPath.asStringView(),
                   absPathRef.asStringView());
    }
    // In practice, CMake ends up passing paths to project files as well
    // as files inside the build root. Normally, files inside the build root
    // are generated ones, but to be safe, check if the corresponding file
    // exists in the project. Since the build root itself is typically inside
    // the project root, check the build root first.
  } else if (auto buildRootRelPath =
                 this->buildRootPath.tryMakeRelative(absPathRef)) {
    auto originalFileSourcePath =
        this->projectRootPath.makeAbsoluteAllowKindMismatch(
            buildRootRelPath.value());
    llvm::SmallString<64> realPath{};
    std::error_code error = llvm::sys::fs::real_path(
        originalFileSourcePath.asStringRef(), realPath);
    // It is possible using symlinks for there to be the situation that
    // projectRoot / relativePath exists, but is actually a symlink to
    // inside the build root, rather than an in-project file. So check that
    // that real_path is the same.
    if (!error && realPath.str() == originalFileSourcePath.asStringRef()) {
      return insertRelPath(RootRelativePathRef(buildRootRelPath->asStringView(),
                                               RootKind::Project),
                           /*isInProject*/ true);
    }
  } else if (auto optProjectRootRelPath =
                 this->projectRootPath.tryMakeRelative(absPathRef)) {
    return insertRelPath(optProjectRootRelPath.value(), /*isInProject*/ true);
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
               FileMetadata{StableFileId{this->storage.back().asRef(),
                                         /*isInProject*/ false,
                                         /*isSynthetic*/ true},
                            absPathRef, optPackageMetadata}})
      .second;
}

void FileMetadataMap::forEachFileId(
    absl::FunctionRef<void(clang::FileID, StableFileId)> callback) {
  for (auto &[wrappedFileId, entry] : this->map) {
    callback(wrappedFileId.data, entry.stableFileId);
  }
}

std::optional<StableFileId>
FileMetadataMap::getStableFileId(clang::FileID fileId) const {
  auto it = this->map.find({fileId});
  if (it == this->map.end()) {
    return {};
  }
  return it->second.stableFileId;
}

const FileMetadata *
FileMetadataMap::getFileMetadata(clang::FileID fileId) const {
  auto it = this->map.find({fileId});
  if (it == this->map.end()) {
    return {};
  }
  return &it->second;
}

} // namespace scip_clang
