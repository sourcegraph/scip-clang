#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "cxxopts.hpp"
#include "doctest/doctest.h"
#include "spdlog/fmt/fmt.h"

#include "clang/Tooling/CompilationDatabase.h"
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
};

// Use YAML instead of JSON for easy line-based diffs (a la insta in Rust-land)
template <> struct llvm::yaml::MappingTraits<clang::tooling::CompileCommand> {
  static void mapping(llvm::yaml::IO &io, clang::tooling::CompileCommand &cmd) {
    io.mapRequired("command", cmd.CommandLine);
    io.mapRequired("file", cmd.Filename);
    io.mapRequired("directory", cmd.Directory);
    io.mapRequired("output", cmd.Output);
  }
};

template <>
struct llvm::yaml::SequenceElementTraits<clang::tooling::CompileCommand> {
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
};

TEST_CASE("COMPDB_PARSING") {
  if (test::globalCliOptions.testKind != test::Kind::CompdbTests) {
    return;
  }

  scip_clang::compdb::ResumableParser parser;

  std::vector<CompDbTestCase> testCases{};
  testCases.push_back(CompDbTestCase{"simple.json", 3, {2, 3, 4}});

  auto dataDir =
      std::filesystem::current_path().append("test").append("compdb");

  for (auto &testCase : testCases) {
    StdPath jsonFilePath = dataDir;
    jsonFilePath.append(testCase.jsonFilename);

    auto compdbFile = compdb::CompilationDatabaseFile::openAndExitOnErrors(
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
      parser.initialize(compdbFile, refillCount);
      std::vector<std::vector<clang::tooling::CompileCommand>> commandGroups;
      std::string buffer;
      llvm::raw_string_ostream outStr(buffer);
      llvm::yaml::Output yamlOut(outStr);
      while (true) {
        std::vector<clang::tooling::CompileCommand> commands;
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

std::string deriveRootFromTUPath(const std::string &tuPath,
                                 const std::string &virtualRoot) {
  llvm::SmallString<64> realPathSmallStr;
  llvm::sys::fs::real_path(tuPath, realPathSmallStr);
  auto realPathStr = realPathSmallStr.str();
  const char key[] = "test/preprocessor/";
  auto startIdx = realPathStr.rfind(key);
  ENFORCE(startIdx != std::string::npos);

  auto scipClangRoot = realPathStr.slice(0, startIdx).str();

  startIdx = virtualRoot.rfind(key);
  ENFORCE(startIdx != std::string::npos);
  auto testRelativeRoot = virtualRoot.substr(startIdx, virtualRoot.size());

  return scipClangRoot + testRelativeRoot + "/";
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
      [](clang::tooling::CompileCommand &&command)
          -> absl::flat_hash_map<RootRelativePath, std::string> {
        TempFile tmpYamlFile(
            fmt::format("{}.yaml", test::globalCliOptions.testName));

        // HACK(def: derive-root-path)
        // Get the real path to the file and compute the root relative
        // to that, instead of using the synthetic sandbox root, because
        // we want the root to be a prefix of the real path (the real path
        // is used when tracking preprocessor history)
        ENFORCE(llvm::sys::path::is_absolute(command.Filename));
        auto derivedRoot =
            ::deriveRootFromTUPath(command.Filename, command.Directory);

        auto mainTuFilePathStr = StdPath(command.Filename)
                                     .lexically_relative(command.Directory)
                                     .string();
        auto mainTuFile = RootRelativePath{
            RootRelativePathRef{mainTuFilePathStr, RootKind::Project}};

        CliOptions cliOptions{};
        cliOptions.workerMode = "testing";
        cliOptions.logLevel = spdlog::level::level_enum::info;
        cliOptions.preprocessorRecordHistoryFilterRegex = ".*";
        cliOptions.preprocessorHistoryLogPath = tmpYamlFile.path;
        auto workerOptions = WorkerOptions::fromCliOptions(cliOptions);
        workerOptions.recordingOptions.preferRelativePaths = true;
        workerOptions.recordingOptions.rootPath = derivedRoot;
        Worker worker(std::move(workerOptions));

        scip::Index index{};
        auto callback = [](SemanticAnalysisJobResult &&,
                           EmitIndexJobDetails &) -> bool { return false; };
        worker.processTranslationUnit(
            SemanticAnalysisJobDetails{std::move(command)}, callback, index);
        worker.flushStreams();
        std::string actual(test::readFileToString(tmpYamlFile.path));

        absl::flat_hash_map<RootRelativePath, std::string> out;
        out.insert({std::move(mainTuFile), std::move(actual)});
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

}

int main(int argc, char *argv[]) {
  scip_clang::initializeSymbolizer(argv[0]);

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
