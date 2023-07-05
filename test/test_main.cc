#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/start_dir.hpp"
#include "cxxopts.hpp"
#include "doctest/doctest.h"
#include "spdlog/fmt/fmt.h"

#include "llvm/Support/YAMLTraits.h"

#include "scip/scip.pb.h"

#include "indexer/CliOptions.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/Enforce.h"
#include "indexer/FileSystem.h"
#include "indexer/Worker.h"

#include "test/Snapshot.h"

namespace scip_clang {
namespace test {

enum class Kind {
  UnitTests,
  CompdbTests,
  PreprocessorTests,
  RobustnessTests,
  IndexTests,
};

struct CliOptions {
  std::string rootDirectory;
  test::Kind testKind;
  std::string testName;
  test::SnapshotMode testMode;
};

static test::CliOptions globalCliOptions{};

} // namespace test
} // namespace scip_clang

using namespace scip_clang;

struct CompDbTestCase {
  std::string jsonFilename;
  size_t checkCount;
  std::vector<size_t> refillCountsToTry;
  compdb::ParseOptions parseOptions;
};

// Use YAML instead of JSON for easy line-based diffs (a la insta in Rust-land)
template <> struct llvm::yaml::MappingTraits<compdb::CommandObject> {
  static void mapping(llvm::yaml::IO &io, compdb::CommandObject &cmd) {
    io.mapRequired("command", cmd.arguments);
    io.mapRequired("filePath", cmd.filePath);
    io.mapRequired("directory", cmd.workingDirectory);
  }
};

template <> struct llvm::yaml::SequenceElementTraits<compdb::CommandObject> {
  static const bool flow = false;
};

TEST_CASE("UNIT_TESTS") {
  if (test::globalCliOptions.testKind != test::Kind::UnitTests) {
    return;
  }
  struct HeaderFilterTestCase {
    std::string regex;
    std::vector<std::string> matchTrue;
    std::vector<std::string> matchFalse;
  };
  std::vector<HeaderFilterTestCase> testCases{
      {R"(.+\.h.*)", {"a.h", "a.hpp", "a.hxx"}, {"a.c", "a.cpp", "a.cxx"}},
      {"foo.h", {"foo.h"}, {"foo.hpp", "foo.hxx", "bar.h"}},
      {"(foo|bar).h", {"foo.h", "bar.h"}, {"qux.h"}},
  };

  for (auto &testCase : testCases) {
    std::string regexCopy = testCase.regex;
    scip_clang::HeaderFilter filter(std::move(testCase.regex));
    for (auto &shouldMatch : testCase.matchTrue) {
      CHECK_MESSAGE(
          filter.matches(shouldMatch),
          fmt::format("expected regex {} to match {}", regexCopy, shouldMatch));
    }
    for (auto &shouldntMatch : testCase.matchFalse) {
      CHECK_MESSAGE(!filter.matches(shouldntMatch),
                    fmt::format("expected regex {} to not match {}", regexCopy,
                                shouldntMatch));
    }
  }

  {
    struct PathNormalizationTestCase {
      std::string path;
      bool isNormalized;
    };
    std::vector<PathNormalizationTestCase> testCases{
        {"/a/b/c", true},
        {"/a/./c", false},
        {"/a/../b", false},
        {"/a///b", false},
    };
    for (auto &testCase : testCases) {
      auto path = AbsolutePath(std::string(testCase.path));
      CHECK_MESSAGE(path.asRef().isNormalized() == testCase.isNormalized,
                    fmt::format("expected {} to be {}normalized", testCase.path,
                                testCase.isNormalized ? "" : "non-"));
    }
  }

  {
    struct PathPrefixesTestCase {
      std::string path;
      std::vector<std::string> prefixes;
    };
    std::vector<PathPrefixesTestCase> testCases{
        {"/a/b", {"/", "/a", "/a/b"}},
    };
    for (auto &testCase : testCases) {
      auto &expectedPrefixes = testCase.prefixes;
      auto path = AbsolutePath(std::string(testCase.path));
      auto start = path.asRef().prefixesBegin();
      auto end = path.asRef().prefixesEnd();
      std::vector<std::string> gotPrefixes;
      for (auto it = start; it != end; ++it) {
        gotPrefixes.push_back(std::string((*it).asStringView()));
      }
      absl::c_reverse(gotPrefixes);
      CHECK_MESSAGE(absl::c_equal(expectedPrefixes, gotPrefixes),
                    fmt::format("expected prefixes: {}\n  actual prefixes: {}",
                                fmt::join(expectedPrefixes, ", "),
                                fmt::join(gotPrefixes, ", ")));
    }
  }
};

TEST_CASE("COMPDB_PARSING") {
  if (test::globalCliOptions.testKind != test::Kind::CompdbTests) {
    return;
  }

  scip_clang::compdb::ResumableParser parser;

  std::vector<CompDbTestCase> testCases{};
  auto testOptions =
      compdb::ParseOptions::create(/*refillCount*/ 1, /*forTesting*/ true);
  testCases.push_back(CompDbTestCase{"simple.json", 3, {2, 3, 4}, testOptions});
  auto testOptionsSkip = testOptions;
  testOptionsSkip.skipNonMainFileEntries = true;
  testCases.push_back(CompDbTestCase{"skipping.json", 9, {4}, testOptionsSkip});

  auto dataDir =
      std::filesystem::current_path().append("test").append("compdb");

  for (auto &testCase : testCases) {
    StdPath jsonFilePath = dataDir;
    jsonFilePath.append(testCase.jsonFilename);

    auto compdbFile = compdb::File::openAndExitOnErrors(
        jsonFilePath,
        compdb::ValidationOptions{.checkDirectoryPathsAreAbsolute = false});
    if (!compdbFile.file) {
      spdlog::error("missing JSON file at path {}", jsonFilePath.c_str());
      REQUIRE(compdbFile.file);
    }
    REQUIRE(compdbFile.sizeInBytes() > 0);
    CHECK_MESSAGE(compdbFile.commandCount() == testCase.checkCount,
                  fmt::format("counted {} jobs but expected {} in {}",
                              compdbFile.commandCount(), testCase.checkCount,
                              jsonFilePath.string()));

    for (auto refillCount : testCase.refillCountsToTry) {
      compdb::ResumableParser parser{};
      auto parseOptions = testCase.parseOptions;
      parseOptions.refillCount = refillCount;
      parser.initialize(compdbFile, parseOptions);
      std::vector<std::vector<compdb::CommandObject>> commandGroups;
      std::string buffer;
      llvm::raw_string_ostream outStr(buffer);
      llvm::yaml::Output yamlOut(outStr);
      while (true) {
        std::vector<compdb::CommandObject> commands;
        parser.parseMore(commands);
        if (commands.size() == 0) {
          break;
        }
        yamlOut << commands;
      }

      std::string yamlFilename = absl::StrReplaceAll(
          testCase.jsonFilename,
          {{".json", fmt::format("-{}.snapshot.yaml", refillCount)}});
      StdPath yamlFilePath = dataDir;
      yamlFilePath.append(yamlFilename);

      test::compareOrUpdateSingleFile(test::globalCliOptions.testMode, buffer,
                                      yamlFilePath);
    }
  }

  return;
}

/// HACK(def: derive-root-path)
///
/// Computes the root inside the project source directory, instead of
/// working within the sandbox directory as created by Bazel.
///
/// \p relativeTestRoot is a path like \c test/kind
/// \p tuPath is a path like \c test/kind/case/.../lol.h
/// \p rootInSandbox is a path like \c /blah/.../x/test/kind/case
///    inside Bazel's build directory.
///
/// \returns A path like \c /home/me/code/scip-clang/test/kind/case/ inside
///   the project source directory (not the build directory).
///
/// This is needed because:
/// - Paths obtained from Clang are "real paths", i.e. symlinks are
///   dereferenced.
/// - scip-clang assumes the current working directory as the
///   project root.
/// - scip-clang checks if the path obtained from Clang is a
///   substring of the project root. This will fail because
///   the path to the TU and any headers will have been
///   dereferenced and point inside the project source directory.
///
/// Another option would be to allow passing in the project root
/// from outside, but that would complicate the indexer logic further.
AbsolutePath deriveRootInSourceDir(RootRelativePathRef testDir,
                                   const RootPath &rootInSandbox,
                                   RootRelativePathRef tuPath) {
  llvm::sys::fs::file_status fileStatus;
  auto tuPathInSandbox = rootInSandbox.makeAbsolute(tuPath);
  llvm::sys::fs::status(tuPathInSandbox.asStringRef(), fileStatus,
                        /*follow*/ false);
  ENFORCE(llvm::sys::fs::is_symlink_file(fileStatus));

  llvm::SmallString<64> realPathSmallStr;
  llvm::sys::fs::real_path(tuPathInSandbox.asStringRef(), realPathSmallStr);
  auto realPathStr = realPathSmallStr.str();
  ENFORCE(llvm::sys::path::is_absolute(realPathStr));

  auto key = testDir.asStringView();
  auto startIdx = realPathStr.rfind(key);
  ENFORCE(startIdx != std::string::npos);

  auto scipClangRoot = realPathStr.slice(0, startIdx).str();
  // scipClangRoot = /home/me/code/scip-clang

  startIdx = rootInSandbox.asRef().asStringView().rfind(key);
  ENFORCE(startIdx != std::string::npos);
  auto testRelativeRoot = rootInSandbox.asRef().asStringView().substr(startIdx);
  // testRelativeRoot = test/kind/case

  return AbsolutePath{
      fmt::format("{}{}/", scipClangRoot, testRelativeRoot, "/")};
}

struct TempFile {
  StdPath path;

  TempFile &operator=(const TempFile &) = delete;
  TempFile(const TempFile &) = delete;
  TempFile(StdPath filename)
      : path(std::filesystem::temp_directory_path() / filename) {}
  ~TempFile() {
    std::filesystem::remove(this->path);
  }
};

TEST_CASE("PREPROCESSING") {
  if (test::globalCliOptions.testKind != test::Kind::PreprocessorTests) {
    return;
  }

  ENFORCE(test::globalCliOptions.testName != "",
          "--test-name should be passed for preprocessor tests");
  StdPath root = std::filesystem::current_path();
  root.append("test");
  root.append("preprocessor");
  root.append(test::globalCliOptions.testName);
  ENFORCE(std::filesystem::exists(root), "missing test directory at {}",
          root.c_str());

  test::MultiTuSnapshotTest myTest{
      RootPath{AbsolutePath{root.string()}, RootKind::Project},
      [](const RootRelativePath &sourceFilePath)
          -> std::optional<RootRelativePath> {
        if (test::isTuMainFilePath(sourceFilePath.asStringRef())) {
          StdPath newPath = sourceFilePath.asStringRef();
          newPath.replace_extension(".preprocessor-history.yaml");
          return RootRelativePath{RootRelativePathRef{
              newPath.c_str(), sourceFilePath.asRef().kind()}};
        }
        return {};
      }};
  myTest.run(
      test::globalCliOptions.testMode,
      [](const RootPath &rootInSandbox, auto &&cmdObjectBuilder)
          -> absl::flat_hash_map<RootRelativePath, std::string> {
        TempFile tmpYamlFile(
            fmt::format("{}.yaml", test::globalCliOptions.testName));

        RootRelativePath key{
            RootRelativePathRef{"test/preprocessor", RootKind::Project}};
        auto tuPath = cmdObjectBuilder.tuPathInSandbox;
        auto rootInSourceDir =
            ::deriveRootInSourceDir(key.asRef(), rootInSandbox, tuPath);

        compdb::CommandObject command = cmdObjectBuilder.build(rootInSandbox);

        CliOptions cliOptions{};
        cliOptions.workerMode = "testing";
        cliOptions.logLevel = spdlog::level::level_enum::info;
        cliOptions.preprocessorRecordHistoryFilterRegex = ".*";
        cliOptions.preprocessorHistoryLogPath = tmpYamlFile.path;
        auto workerOptions = WorkerOptions::fromCliOptions(cliOptions);
        workerOptions.recordingOptions.preferRelativePaths = true;
        workerOptions.recordingOptions.rootPath = rootInSourceDir.asStringRef();
        Worker worker(std::move(workerOptions));

        auto callback = [](SemanticAnalysisJobResult &&,
                           EmitIndexJobDetails &) -> bool { return false; };
        TuIndexingOutput tuIndexingOutput{};
        worker.processTranslationUnit(
            SemanticAnalysisJobDetails{std::move(command)}, callback,
            tuIndexingOutput);
        worker.flushStreams();
        std::string actual(test::readFileToString(tmpYamlFile.path));

        absl::flat_hash_map<RootRelativePath, std::string> out;
        out.insert({RootRelativePath{tuPath}, std::move(actual)});
        return out;
      });
}

TEST_CASE("ROBUSTNESS") {
  if (test::globalCliOptions.testKind != test::Kind::RobustnessTests) {
    return;
  }
  auto fault = test::globalCliOptions.testName;
  if (!(fault == "crash" || fault == "sleep" || fault == "spin")) {
    return;
  }
  std::vector<std::string> args;
  args.push_back("./indexer/scip-clang");
  args.push_back("--compdb-path=test/robustness/compile_commands.json");
  args.push_back("--log-level=warning");
  args.push_back("--force-worker-fault=" + fault);
  args.push_back("--testing");
  args.push_back("--receive-timeout-seconds=3");
  args.push_back(fmt::format("--driver-id=robustness-{}", fault));
  // Stack trace printed by abseil is not portable for snapshots
  args.push_back("--no-stack-trace");
  TempFile tmpLogFile(fmt::format("{}.tmp.log", fault));
  boost::process::child driver(args,
                               boost::process::std_out > boost::process::null,
                               boost::process::std_err > tmpLogFile.path);
  driver.wait();

  auto log = test::readFileToString(tmpLogFile.path);
  std::vector<std::string_view> splitLines = absl::StrSplit(log, "\n");
  // Can't figure out how to turn off ASan for the crashWorker() function,
  // but it is useful for tests to pass with ASan too.
  std::vector<std::string_view> actualLogLines;
  for (auto &line : splitLines) {
    if (absl::StrContains(line, "] driver")
        || absl::StrContains(line, "] worker")) {
      actualLogLines.push_back(line);
    }
  }
  log = absl::StrJoin(actualLogLines, "\n");

  StdPath snapshotLogPath = "./test/robustness";
  snapshotLogPath.append(fault + ".snapshot.log");
  test::compareOrUpdateSingleFile(test::globalCliOptions.testMode, log,
                                  snapshotLogPath);
}

TEST_CASE("INDEX") {
  if (test::globalCliOptions.testKind != test::Kind::IndexTests) {
    return;
  }

  ENFORCE(test::globalCliOptions.testName != "",
          "--test-name should be passed for index tests");
  StdPath root = std::filesystem::current_path();
  root.append("test");
  root.append("index");
  root.append(test::globalCliOptions.testName);
  ENFORCE(std::filesystem::exists(root), "missing test directory at {}",
          root.c_str());

  test::MultiTuSnapshotTest myTest{
      RootPath{AbsolutePath{root.string()}, RootKind::Project},
      [](const RootRelativePath &sourceFilePath)
          -> std::optional<RootRelativePath> {
        std::string newExtension = ".snapshot";
        newExtension.append(sourceFilePath.asRef().extension());
        auto newPath = RootRelativePath{sourceFilePath.asRef()};
        newPath.replaceExtension(newExtension);
        return newPath;
      }};
  myTest.runWithMerging(
      test::globalCliOptions.testMode,
      [](const RootPath &rootInSandbox,
         auto &&compdbBuilder) -> test::MultiTuSnapshotTest::MergeResult {
        TempFile tmpCompdb{
            fmt::format("{}-compdb.json", test::globalCliOptions.testName)};
        AbsolutePath compdbPath{tmpCompdb.path.string()};
        {
          std::error_code error;
          llvm::raw_fd_ostream compdb{llvm::StringRef(compdbPath.asStringRef()),
                                      error};
          ENFORCE(!error, "failed to open temporary file for compdb at {}",
                  compdbPath.asStringRef());
          compdb << compdbBuilder.toJSON(rootInSandbox);
        }

        RootRelativePath key{
            RootRelativePathRef{"test/index", RootKind::Project}};
        auto rootInSourceDir =
            ::deriveRootInSourceDir(key.asRef(), rootInSandbox,
                                    compdbBuilder.entries[0].tuPathInSandbox);

        TempFile scipIndexFile{
            fmt::format("{}.scip", test::globalCliOptions.testName)};
        auto scipIndexPath = scipIndexFile.path.string();
        std::vector<std::string> args;
        args.push_back(
            (std::filesystem::current_path() / "indexer/scip-clang").string());
        args.push_back(
            fmt::format("--compdb-path={}", compdbPath.asStringRef()));
        args.push_back(fmt::format("--index-output-path={}", scipIndexPath));
        args.push_back("--log-level=warning");
        args.push_back("--receive-timeout-seconds=60");
        args.push_back(fmt::format("--driver-id=index-{}",
                                   test::globalCliOptions.testName));
        args.push_back("--deterministic");
        args.push_back("--no-progress-report");
        auto packageMapPath =
            StdPath(rootInSourceDir.asStringRef()) / "package-map.json";
        if (std::filesystem::exists(packageMapPath)) {
          args.push_back(
              fmt::format("--package-map-path={}", packageMapPath.string()));
        }
        boost::process::child driver(
            args, boost::process::start_dir(rootInSourceDir.asStringRef()),
            boost::process::std_out > stdout, boost::process::std_err > stderr);
        driver.wait();

        scip::Index index{};
        std::ifstream inputStream(scipIndexPath,
                                  std::ios_base::in | std::ios_base::binary);
        REQUIRE_MESSAGE(
            !inputStream.fail(),
            fmt::format("failed to open index file at '{}'", scipIndexPath));
        auto parseSuccess = index.ParseFromIstream(&inputStream);
        REQUIRE_MESSAGE(
            parseSuccess,
            fmt::format("failed to parse SCIP index at '{}'", scipIndexPath));

        llvm::SmallString<128> pathBuf;
        llvm::sys::fs::real_path(scipIndexPath, pathBuf);

        absl::flat_hash_map<RootRelativePath, std::string> snapshots;
        RootPath testRoot{AbsolutePath{rootInSourceDir}, RootKind::Project};
        for (auto &doc : index.documents()) {
          std::string buffer;
          llvm::raw_string_ostream os(buffer);
          auto docAbsPath = testRoot.makeAbsolute(
              RootRelativePathRef{doc.relative_path(), RootKind::Project});
          test::SnapshotPrinter::printDocument(doc, docAbsPath.asRef(), os);
          os.flush();
          RootRelativePath path{
              RootRelativePathRef{doc.relative_path(), RootKind::Project}};
          ENFORCE(!buffer.empty());
          snapshots.insert({std::move(path), std::move(buffer)});
        }
        std::vector<scip::SymbolInformation> externalSymbols{};
        absl::c_move(std::move(*index.mutable_external_symbols()),
                     std::back_inserter(externalSymbols));
        return {std::move(snapshots), std::move(externalSymbols)};
      });
}

int main(int argc, char *argv[]) {
  scip_clang::initializeSymbolizer(argv[0], /*printStacktrace*/ true);
  cxxopts::Options options("test_main", "Test runner for scip-clang");
  std::string testKind;
  options.add_options()("test-kind",
                        "One of 'unit', 'compdb' or 'preprocessor'",
                        cxxopts::value<std::string>(testKind));
  options.add_options()(
      "test-name", "(Optional) Separate identifier for a specific test",
      cxxopts::value<std::string>(test::globalCliOptions.testName)
          ->default_value(""));
  options.add_options()("update",
                        "Should snapshots be updated instead of comparing?",
                        cxxopts::value<bool>());

  auto result = options.parse(argc, argv);

  if (testKind.empty()) {
    fmt::print(stderr, "Missing --test-kind argument to test runner");
    std::exit(EXIT_FAILURE);
  }
  if (testKind == "unit") {
    test::globalCliOptions.testKind = test::Kind::UnitTests;
  } else if (testKind == "compdb") {
    test::globalCliOptions.testKind = test::Kind::CompdbTests;
  } else if (testKind == "preprocessor") {
    test::globalCliOptions.testKind = test::Kind::PreprocessorTests;
  } else if (testKind == "robustness") {
    test::globalCliOptions.testKind = test::Kind::RobustnessTests;
  } else if (testKind == "index") {
    test::globalCliOptions.testKind = test::Kind::IndexTests;
  } else {
    fmt::print(stderr, "Unknown value for --test-kind");
    std::exit(EXIT_FAILURE);
  }

  test::globalCliOptions.testMode = test::SnapshotMode::Compare;
  if (result.count("update") > 0) {
    test::globalCliOptions.testMode = test::SnapshotMode::Update;
  }

  doctest::Context context(argc, argv);
  return context.run();
}
