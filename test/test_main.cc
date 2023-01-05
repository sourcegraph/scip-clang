#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string_view>

#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "cxxopts.hpp"
#include "doctest.h"
#include "dtl/dtl.hpp"
#include "spdlog/fmt/fmt.h"

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"

#include "indexer/CliOptions.h"
#include "indexer/CompilationDatabase.h"
#include "indexer/Enforce.h"
#include "indexer/FileSystem.h"
#include "indexer/Worker.h"

using namespace scip_clang;

// Perform a line-wise diff of expected vs actual.
//
// NOTE(ref: based-on-sorbet): This function implementation was originally
// in Sorbet's expectations.cc named as CHECK_EQ_DIFF
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

enum class SnapshotTestMode {
  Compare,
  Update,
};

enum class TestKind {
  UnitTests,
  CompdbTests,
  PreprocessorTests,
};

struct TestCliOptions {
  TestKind testKind;
  std::string testName;
  SnapshotTestMode testMode;
};

static TestCliOptions testCliOptions{};

static std::string readFileToString(const StdPath &path) {
  std::ifstream in(path.c_str(), std::ios_base::in | std::ios_base::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

void compareOrUpdate(std::string_view actual,
                     std::filesystem::path snapshotFilepath) {
  switch (testCliOptions.testMode) {
  case SnapshotTestMode::Compare: {
    std::string expected(::readFileToString(snapshotFilepath));
    compareDiff(expected, actual, "comparison failed");
    return;
  }
  case SnapshotTestMode::Update: {
    std::ofstream out(snapshotFilepath.c_str(),
                      std::ios_base::out | std::ios_base::binary);
    out.write(actual.data(), actual.size());
  }
  }
}

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
  if (testCliOptions.testKind != TestKind::UnitTests) {
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
          filter.isMatch(shouldMatch),
          fmt::format("expected regex {} to match {}", regexCopy, shouldMatch));
    }
    for (auto &shouldntMatch : testCase.matchFalse) {
      CHECK_MESSAGE(!filter.isMatch(shouldntMatch),
                    fmt::format("expected regex {} to not match {}", regexCopy,
                                shouldntMatch));
    }
  }
};

TEST_CASE("COMPDB_PARSING") {
  if (testCliOptions.testKind != TestKind::CompdbTests) {
    return;
  }

  scip_clang::compdb::ResumableParser parser;

  std::vector<CompDbTestCase> testCases{};
  testCases.push_back(CompDbTestCase{"simple.json", 3, {2, 3, 4}});

  auto dataDir =
      std::filesystem::current_path().append("test").append("compdb");

  for (auto &testCase : testCases) {
    std::filesystem::path jsonFilepath = dataDir;
    jsonFilepath.append(testCase.jsonFilename);

    std::error_code ec;
    auto compdbFile = compdb::CompilationDatabaseFile::open(jsonFilepath, ec);
    REQUIRE(!ec);
    if (!compdbFile.file) {
      spdlog::error("missing JSON file at path {}", jsonFilepath.c_str());
      REQUIRE(compdbFile.file);
    }
    REQUIRE(compdbFile.sizeInBytes);
    CHECK_MESSAGE(compdbFile.numJobs == testCase.checkCount,
                  fmt::format("counted {} jobs but expected {} in {}",
                              compdbFile.numJobs, testCase.checkCount,
                              jsonFilepath.string()));

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
      std::filesystem::path yamlFilePath = dataDir;
      yamlFilePath.append(yamlFilename);

      compareOrUpdate(buffer, yamlFilePath);
    }
  }

  return;
}

static std::vector<std::filesystem::path>
listFilesRecursive(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> out;
  std::filesystem::recursive_directory_iterator it(root);
  for (auto &dirEntry : it) {
    if (!dirEntry.is_directory()) {
      out.push_back(dirEntry.path());
    }
  }
  absl::c_sort(out);
  return out;
}

class SnapshotTest final {
  StdPath rootPath;
  struct TranlationUnitInputOutput {
    StdPath translationUnitMainFilePath;
    StdPath snapshotPath;
  };

  std::vector<TranlationUnitInputOutput> inputOutputs;

public:
  SnapshotTest(StdPath &&root,
               absl::FunctionRef<StdPath(const StdPath &)> tuPathToSnapshotPath)
      : rootPath(root), inputOutputs() {
    auto inputFiles = ::listFilesRecursive(this->rootPath);
    for (auto &inputFile : inputFiles) {
      if (SnapshotTest::isTuMainFilePath(inputFile)) {
        auto snapshotPath = tuPathToSnapshotPath(inputFile);
        this->inputOutputs.emplace_back(TranlationUnitInputOutput{
            std::move(inputFile), std::move(snapshotPath)});
      }
    }
  }

  void testCompareOrUpdate(
      absl::FunctionRef<std::string(clang::tooling::CompileCommand &&command)>
          compute) {
    for (auto &io : this->inputOutputs) {
      clang::tooling::CompileCommand command{};
      command.Directory = rootPath.string();
      auto tuMainPath = io.translationUnitMainFilePath;
      tuMainPath.lexically_relative(rootPath);
      command.CommandLine = {
          "clang",
          "-I",
          ".",
          tuMainPath.string(),
      };
      command.Filename = tuMainPath.string();
      auto output = compute(std::move(command));
      ::compareOrUpdate(output, io.snapshotPath);
    }
  }

private:
  static bool isTuMainFilePath(const StdPath &p) {
    auto ext = p.extension();
    return ext == ".cc" || ext == ".c";
  }
};

static std::function<StdPath(const StdPath &)>
replaceExtension(std::string newExt) {
  return [=](const StdPath &tuMainFilePath) -> StdPath {
    StdPath snapshotPath = tuMainFilePath;
    snapshotPath.replace_extension(newExt);
    return snapshotPath;
  };
}

TEST_CASE("PREPROCESSING") {
  if (testCliOptions.testKind != TestKind::PreprocessorTests) {
    return;
  }

  ENFORCE(testCliOptions.testName != "",
          "--test-name should be passed for preprocessor tests");
  std::filesystem::path root = std::filesystem::current_path();
  root.append("test");
  root.append("preprocessor");
  root.append(testCliOptions.testName);
  ENFORCE(std::filesystem::exists(root), "missing test directory at {}",
          root.c_str());

  SnapshotTest(std::move(root),
               ::replaceExtension(".preprocessor-history.yaml"))
      .testCompareOrUpdate(
          [](clang::tooling::CompileCommand &&command) -> std::string {
            auto tmpYamlPath = std::filesystem::temp_directory_path();
            tmpYamlPath.append(fmt::format("{}.yaml", testCliOptions.testName));
            Worker worker(WorkerOptions{
                IpcOptions::testingStub,
                spdlog::level::level_enum::info,
                true,
                ".*",
                tmpYamlPath,
            });
            SemanticAnalysisJobResult result{};
            worker.performSemanticAnalysis(
                SemanticAnalysisJobDetails{std::move(command)}, result);
            worker.flushStreams();
            std::string actual(::readFileToString(tmpYamlPath));
            std::filesystem::remove(tmpYamlPath);
            return actual;
          });
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
      cxxopts::value<std::string>(testCliOptions.testName)->default_value(""));
  options.add_options()("update",
                        "Should snapshots be updated instead of comparing?",
                        cxxopts::value<bool>());

  auto result = options.parse(argc, argv);

  if (testKind.empty()) {
    fmt::print(stderr, "Missing --test-kind argument to test runner");
    std::exit(EXIT_FAILURE);
  }
  if (testKind == "unit") {
    testCliOptions.testKind = TestKind::UnitTests;
  } else if (testKind == "compdb") {
    testCliOptions.testKind = TestKind::CompdbTests;
  } else if (testKind == "preprocessor") {
    testCliOptions.testKind = TestKind::PreprocessorTests;
  } else {
    fmt::print(stderr, "Unknown value for --test-kind");
    std::exit(EXIT_FAILURE);
  }

  testCliOptions.testMode = SnapshotTestMode::Compare;
  if (result.count("update") > 0) {
    testCliOptions.testMode = SnapshotTestMode::Update;
  }

  doctest::Context context(argc, argv);
  return context.run();
}
