#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "doctest/doctest.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsuggest-override"
#include "dtl/dtl.hpp"
#pragma clang diagnostic pop
#include "spdlog/fmt/fmt.h"

#include "llvm/Support/raw_ostream.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/Enforce.h"
#include "indexer/FileSystem.h"

#include "test/Snapshot.h"

using namespace scip_clang;

template <typename T> using Repeated = google::protobuf::RepeatedField<T>;

static bool isSCIPRangeLess(const Repeated<int32_t> &a,
                            const Repeated<int32_t> &b) {
  if (a[0] != b[0]) { // start line
    return a[0] < b[0];
  }
  if (a[1] != b[1]) { // start column
    return a[1] < b[1];
  }
  if (a.size() != b.size()) { // is one of these multiline
    return a.size() < b.size();
  }
  if (a[2] != b[2]) { // end line
    return a[2] < b[2];
  }
  if (a.size() == 4) {
    return a[3] < b[3];
  }
  return false;
}

namespace {

struct SCIPPosition final {
  int32_t line;   // 1-based
  int32_t column; // 1-based
};

struct SCIPRange final {
  SCIPPosition start;
  SCIPPosition end;

  SCIPRange(SCIPPosition start, SCIPPosition end) : start(start), end(end) {}

  SCIPRange(const google::protobuf::RepeatedField<int32_t> &protoRange) {
    auto &r = protoRange;
    if (r.size() == 4) {
      *this = SCIPRange(SCIPPosition{r[0] + 1, r[1] + 1},
                        SCIPPosition{r[2] + 1, r[3] + 1});
    }
    *this = SCIPRange(SCIPPosition{r[0] + 1, r[1] + 1},
                      SCIPPosition{r[0] + 1, r[2] + 1});
  }

  bool isMultiline() const {
    return start.line != end.line;
  }

  std::string toString() const {
    auto &r = *this;
    return fmt::format("{}:{}-{}:{}", r.start.line, r.start.column, r.end.line,
                       r.end.column);
  }
};

} // namespace

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

bool isTuMainFilePath(std::string_view p) {
  auto dotIndex = p.rfind('.');
  if (dotIndex == std::string::npos) {
    return false;
  }
  auto ext = p.substr(dotIndex);
  return ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c";
}

void formatSnapshot(const scip::Document &document,
                    AbsolutePathRef sourceFilePath, FormatOptions options,
                    llvm::raw_ostream &out) {
  absl::flat_hash_map<std::string, scip::SymbolInformation> symbolTable{};
  symbolTable.reserve(document.symbols_size());
  for (auto &symbolInfo : document.symbols()) {
    symbolTable.insert({symbolInfo.symbol(), symbolInfo});
  }
  std::vector<scip::Occurrence> occurrences;
  occurrences.reserve(document.occurrences_size());
  for (auto &occ : document.occurrences()) {
    occurrences.push_back(occ);
  }
  absl::c_sort(
      occurrences,
      [](const scip::Occurrence &occ1, const scip::Occurrence &occ2) -> bool {
        return isSCIPRangeLess(occ1.range(), occ2.range());
      });
  auto formatSymbol = [](const std::string &symbol) -> std::string {
    // Strip out repeating information for cleaner snapshots.
    return absl::StrReplaceAll(symbol, {
                                           {"c . ", ""}, // indexer prefix
                                           {"todo-pkg todo-version", "[..]"},
                                       });
  };
  size_t occIndex = 0;
  std::ifstream input(sourceFilePath.asStringView());
  ENFORCE(input.is_open(),
          "failed to open document at '{}' to read source code",
          sourceFilePath.asStringView());
  int32_t lineNumber = 1;
  std::vector<scip::Relationship> relationships;
  for (std::string line; std::getline(input, line); lineNumber++) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    out << "  "; // For '//'
    out << absl::StrReplaceAll(line, {{"\t", " "}});
    out << '\n';
    for (; occIndex < occurrences.size()
           && occurrences[occIndex].range()[0] == lineNumber - 1;
         occIndex++) {
      auto occ = occurrences[occIndex];
      auto range = SCIPRange(occ.range());
      ENFORCE(!range.isMultiline(),
              "TODO: add support for formatting multiline ranges");

      bool isDefinition = ((unsigned(occ.symbol_roles())
                            & unsigned(scip::SymbolRole::Definition))
                           > 0);

      std::string symbolRole = "";
      if (!isDefinition
          && (occ.symbol_roles() & scip::SymbolRole::WriteAccess)) {
        symbolRole = (occ.symbol_roles() & scip::SymbolRole::ReadAccess)
                         ? "(read+write) "
                         : "(write) ";
      }

      ENFORCE(range.start.column < range.end.column,
              "We shouldn't be emitting empty ranges 🙅");

      auto lineStart =
          absl::StrCat("//", std::string(range.start.column - 1, ' '));
      out << lineStart
          << std::string(range.end.column - range.start.column, '^') << ' '
          << std::string(isDefinition ? "definition" : "reference") << ' '
          << symbolRole << formatSymbol(occ.symbol()) << '\n';

      auto printDocs = [&](auto docs, std::string header) -> void {
        if (!options.showDocs)
          return;
        for (auto &doc : docs) {
          out << lineStart << header << '\n';
          auto docstream = std::istringstream(doc);
          for (std::string docline; std::getline(docstream, docline);) {
            out << lineStart << "| " << docline << '\n';
          }
        }
      };
      printDocs(occ.override_documentation(), "override_documentation");
      if (!symbolTable.contains(occ.symbol())) {
        continue;
      }
      auto &symbolInfo = symbolTable[occ.symbol()];
      bool isDefinedByAnother = absl::c_any_of(
          symbolInfo.relationships(),
          [](const auto &rel) -> bool { return rel.is_definition(); });
      if (!isDefinition && !isDefinedByAnother) {
        continue;
      }
      printDocs(symbolInfo.documentation(), "documentation");

      relationships.clear();
      relationships.reserve(symbolInfo.relationships_size());
      for (auto &rel : symbolInfo.relationships()) {
        relationships.push_back(rel);
      }
      absl::c_sort(relationships,
                   [](const scip::Relationship &r1,
                      const scip::Relationship &r2) -> bool {
                     return r1.symbol() < r2.symbol();
                   });
      if (!relationships.empty()) {
        out << lineStart << "relation ";
        for (size_t i = 0; i < relationships.size(); ++i) {
          auto &rel = relationships[i];
          if (rel.is_implementation()) {
            out << "implementation=";
          }
          if (rel.is_reference()) {
            out << "reference=";
          }
          if (rel.is_type_definition()) {
            out << "type_definition=";
          }
          if (rel.is_definition()) {
            out << "definition=";
          }
          out << formatSymbol(rel.symbol());
          if (i != relationships.size() - 1) {
            out << ' ';
          }
        }
        out << '\n';
      }
    }
  }
}

void compareOrUpdateSingleFile(SnapshotMode mode, std::string_view actual,
                               const StdPath &snapshotFilePath) {
  switch (mode) {
  case SnapshotMode::Compare: {
    std::string expected(test::readFileToString(snapshotFilePath));
    test::compareDiff(expected, actual, "comparison failed");
    return;
  }
  case SnapshotMode::Update: {
    std::ofstream out(snapshotFilePath.c_str(),
                      std::ios_base::out | std::ios_base::binary);
    out.write(actual.data(), actual.size());
  }
  }
}

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

MultiTuSnapshotTest::MultiTuSnapshotTest(
    RootPath &&root,
    absl::FunctionRef<std::optional<RootRelativePath>(const RootRelativePath &)>
        getSnapshotPath)
    : rootPath(std::move(root)), inputOutputs() {
  auto inputFiles = ::listFilesRecursive(this->rootPath);
  for (auto &inputFile : inputFiles) {
    if (absl::StrContains(inputFile.asStringRef(), ".snapshot")) {
      continue;
    }
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
    std::vector<std::string> commandLine{"clang", "-I", ".", sourceFilePath};
    {
      std::ifstream tuStream(sourceFilePath, std::ios::in | std::ios::binary);
      std::string prefix = "// extra-args: ";
      for (std::string line; std::getline(tuStream, line);) {
        if (!line.starts_with(prefix)) {
          break;
        }
        for (auto &arg : absl::StrSplit(line.substr(prefix.size()), ' ')) {
          auto s = absl::StripAsciiWhitespace(arg);
          if (!s.empty()) {
            commandLine.emplace_back(s);
          }
        }
      }
    }

    auto output =
        compute(rootPath, io.sourceFilePath.asRef(), std::move(commandLine));

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

} // namespace test
} // namespace scip_clang