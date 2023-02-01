#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "doctest/doctest.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsuggest-override"
#include "dtl/dtl.hpp"
#pragma clang diagnostic pop
#include "spdlog/fmt/fmt.h"

#include "indexer/AbslExtras.h"
#include "indexer/Enforce.h"
#include "indexer/FileSystem.h"

#include "test/Snapshot.h"

using namespace scip_clang;

void compareDiff(std::string_view expected, std::string_view actual,
                 std::string_view errorMessage) {
  if (expected == actual) {
    return;
  }

  std::vector<std::string> expectedLines = absl::StrSplit(expected, '\n');
  std::vector<std::string> actualLines = absl::StrSplit(actual, '\n');
  dtl::Diff<std::string, std::vector<std::string>> diff(expectedLines,
                                                        actualLines);
  diff.compose();
  diff.composeUnifiedHunks();

  std::stringstream ss;
  diff.printUnifiedFormat(ss);
  FAIL_CHECK(fmt::format("{}\n{}", errorMessage, ss.str()));
}

static std::vector<RootRelativePath> listFilesRecursive(const RootPath &root) {
  std::vector<RootRelativePath> out;
  std::filesystem::recursive_directory_iterator it(root.asRef().asStringView());
  for (auto &dirEntry : it) {
    if (!dirEntry.is_directory()) {
      std::string path = dirEntry.path().string();
      if (llvm::sys::path::is_absolute(path)) {
        out.emplace_back(
            root.tryMakeRelative(
                    AbsolutePathRef::tryFrom(std::string_view(path)).value())
                .value());
      } else {
        out.emplace_back(RootRelativePathRef{path, root.kind()});
      }
    }
  }
  absl::c_sort(out);
  return out;
}

namespace scip_clang {
namespace test {

std::string readFileToString(const StdPath &path) {
  std::ifstream in(path.c_str(), std::ios_base::in | std::ios_base::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

void compareOrUpdateSingleFile(SnapshotMode mode, std::string_view actual,
                               const StdPath &snapshotFilePath) {
  switch (mode) {
  case SnapshotMode::Compare: {
    std::string expected(test::readFileToString(snapshotFilePath));
    ::compareDiff(expected, actual, "comparison failed");
    return;
  }
  case SnapshotMode::Update: {
    std::ofstream out(snapshotFilePath.c_str(),
                      std::ios_base::out | std::ios_base::binary);
    out.write(actual.data(), actual.size());
  }
  }
}

MultiTuSnapshotTest::MultiTuSnapshotTest(
    RootPath &&root,
    absl::FunctionRef<std::optional<RootRelativePath>(const RootRelativePath &)>
        getSnapshotPath)
    : rootPath(std::move(root)), inputOutputs() {
  auto inputFiles = ::listFilesRecursive(this->rootPath);
  for (auto &inputFile : inputFiles) {
    if (auto optSnapshotPath = getSnapshotPath(inputFile)) {
      this->inputOutputs.emplace_back(InputOutput{
          std::move(inputFile), std::move(optSnapshotPath.value())});
    }
  }
}

void MultiTuSnapshotTest::run(SnapshotMode mode,
                              RunCompileCommandCallback compute) {
  absl::flat_hash_map<RootRelativePathRef, RootRelativePathRef>
      inputToOutputMap;
  for (auto &io : this->inputOutputs) {
    inputToOutputMap.insert(
        {io.sourceFilePath.asRef(), io.snapshotPath.asRef()});
  }

  for (auto &io : this->inputOutputs) {
    auto &sourceFilePath = io.sourceFilePath.asStringRef();
    if (!test::isTuMainFilePath(sourceFilePath)) {
      continue;
    }
    clang::tooling::CompileCommand command{};
    command.Directory = rootPath.asRef().asStringView();
    command.CommandLine = {
        "clang",
        "-I",
        ".",
        sourceFilePath,
    };
    command.Filename =
        rootPath.makeAbsolute(io.sourceFilePath.asRef()).asStringRef();
    auto output = compute(std::move(command));

    extractTransform(
        std::move(output), /*deterministic*/ true,
        absl::FunctionRef<void(RootRelativePath &&, std::string &&)>(
            [&](auto &&inputPath, auto &&snapshotContent) -> void {
              auto it = inputToOutputMap.find(inputPath.asRef());
              ENFORCE(it != inputToOutputMap.end());
              auto absPath = rootPath.makeAbsolute(it->second);
              test::compareOrUpdateSingleFile(mode, snapshotContent,
                                              absPath.asStringRef());
            }));
  }
}

bool isTuMainFilePath(std::string_view p) {
  auto dotIndex = p.rfind('.');
  if (dotIndex == std::string::npos) {
    return false;
  }
  auto ext = p.substr(dotIndex);
  return ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c";
}

} // namespace test
} // namespace scip_clang