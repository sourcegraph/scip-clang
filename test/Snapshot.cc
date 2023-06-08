#include <compare>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "doctest/doctest.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsuggest-override"
#include "dtl/dtl.hpp"
#pragma clang diagnostic pop
#include "spdlog/fmt/fmt.h"

#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "scip/scip.pb.h"

#include "indexer/AbslExtras.h"
#include "indexer/Enforce.h"
#include "indexer/FileSystem.h"
#include "indexer/IpcMessages.h"
#include "indexer/ScipExtras.h"

#include "test/Snapshot.h"

using namespace scip_clang;

template <typename T> using Repeated = google::protobuf::RepeatedField<T>;

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

static std::string formatSymbol(const std::string &symbol) {
  // Strip out repeating information for cleaner snapshots.
  return absl::StrReplaceAll(symbol, {
                                         {"cxx . . $ ", "[..] "},
                                         {"cxx . ", ""}, // indexer prefix
                                         {"todo-pkg todo-version", "[..]"},
                                         {"test-pkg test-version $", "[..] "},
                                     });
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

// static
FormatOptions SnapshotPrinter::readFormatOptions(AbsolutePathRef path) {
  std::ifstream tuStream(path.asStringView(), std::ios::in | std::ios::binary);
  std::string prefix = "// format-options:";
  test::FormatOptions formatOptions{};
  for (std::string line; std::getline(tuStream, line);) {
    if (!line.starts_with(prefix)) {
      continue;
    }
    for (auto &arg : absl::StrSplit(line.substr(prefix.size()), ',')) {
      auto s = absl::StripAsciiWhitespace(arg);
      if (s == "showDocs") {
        formatOptions.showDocs = true;
      } else {
        FAIL("unknown value in format-options");
      }
    }
  }
  return formatOptions;
}

void SnapshotPrinter::printDocs(
    std::string_view header,
    const google::protobuf::RepeatedPtrField<std::string> &docs) {
  if (!this->options.showDocs)
    return;
  for (auto &doc : docs) {
    this->out << this->lineStart << header << '\n';
    auto docstream = std::istringstream(doc);
    for (std::string docline; std::getline(docstream, docline);) {
      this->out << this->lineStart << "| " << docline << '\n';
    }
  }
}

void SnapshotPrinter::printRelationships(
    const scip::SymbolInformation &symbolInfo) {
  std::vector<scip::Relationship> relationships;
  relationships.clear();
  relationships.reserve(symbolInfo.relationships_size());
  for (auto &rel : symbolInfo.relationships()) {
    relationships.push_back(rel);
  }
  absl::c_sort(
      relationships,
      [](const scip::Relationship &r1, const scip::Relationship &r2) -> bool {
        return r1.symbol() < r2.symbol();
      });
  for (size_t i = 0; i < relationships.size(); ++i) {
    this->out << this->lineStart << "relation ";
    auto &rel = relationships[i];
    std::vector<const char *> kinds{};
#define ADD_KIND(kind_)      \
  if (rel.is_##kind_()) {    \
    kinds.push_back(#kind_); \
  }
    ADD_KIND(implementation)
    ADD_KIND(reference)
    ADD_KIND(type_definition)
    ADD_KIND(definition)
    this->out << absl::StrJoin(kinds, "+") << ' '
              << ::formatSymbol(rel.symbol()) << '\n';
  }
}

// static
std::string SnapshotPrinter::formatExternalSymbols(
    std::vector<scip::SymbolInformation> &&externalSymbols) {
  std::string buf;
  llvm::raw_string_ostream out(buf);
  std::string lineStart = "// ";
  SnapshotPrinter printer{out, lineStart, FormatOptions{.showDocs = true}};
  for (auto &extSym : externalSymbols) {
    out << lineStart << ::formatSymbol(extSym.symbol()) << '\n';
    printer.printDocs("documentation", extSym.documentation());
    printer.printRelationships(extSym);
  }
  return buf;
}

// static
void SnapshotPrinter::printDocument(const scip::Document &document,
                                    AbsolutePathRef sourceFilePath,
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
      [](const scip::Occurrence &lhs, const scip::Occurrence &rhs) -> bool {
        return scip::compareOccurrences(lhs, rhs) == std::strong_ordering::less;
      });
  std::ifstream input(sourceFilePath.asStringView());
  ENFORCE(input.is_open(),
          "failed to open document at '{}' to read source code",
          sourceFilePath.asStringView());
  auto options = SnapshotPrinter::readFormatOptions(sourceFilePath);
  size_t occIndex = 0;
  int32_t lineNumber = 1;
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

      ENFORCE(range.start.column < range.end.column
                  || (range.start.line == 1 && range.end.line == 1
                      && range.start.column == 1),
              "Found empty range for {} at {}:{}:{}", occ.symbol(),
              sourceFilePath.asStringView(), range.start.line,
              range.start.column);

      auto lineStart =
          absl::StrCat("//", std::string(range.start.column - 1, ' '));
      out << lineStart
          << std::string(range.end.column - range.start.column, '^') << ' '
          << std::string(isDefinition ? "definition" : "reference") << ' '
          << symbolRole << ::formatSymbol(occ.symbol()) << '\n';

      SnapshotPrinter printer{out, lineStart, options};
      printer.printDocs("override_documentation", occ.override_documentation());
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
      printer.printDocs("documentation", symbolInfo.documentation());
      printer.printRelationships(symbolInfo);
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

compdb::CommandObject
CommandObjectBuilder::build(const RootPath &rootInSandbox) {
  return compdb::CommandObject{
      this->index, std::string(rootInSandbox.asRef().asStringView()),
      rootInSandbox.makeAbsolute(this->tuPathInSandbox).asStringRef(),
      std::move(this->commandLine)};
}

llvm::json::Value
CompilationDatabaseBuilder::toJSON(const RootPath &rootInSandbox) {
  std::vector<llvm::json::Value> jsonEntries;
  for (auto &builder : this->entries) {
    jsonEntries.emplace_back(llvm::json::Value(builder.build(rootInSandbox)));
  }
  return llvm::json::Value(std::move(jsonEntries));
}

void MultiTuSnapshotTest::run(SnapshotMode mode,
                              RunCompileCommandCallback compute) {
  auto inputToOutputMap = this->buildInputToOutputMap();
  absl::flat_hash_map<RootRelativePath, RootRelativePath> alreadyUsedFiles;

  this->iterateOverTus([&](CommandObjectBuilder &&entryBuilder) -> void {
    auto tuPath = RootRelativePath{entryBuilder.tuPathInSandbox};
    auto output = compute(this->rootPath, std::move(entryBuilder));

    for (auto &[filePath, _] : output) {
      ENFORCE(!alreadyUsedFiles.contains(filePath),
              "{} is (potentially indirectly) included by {} and {}; so "
              "snapshot output will be overwritten",
              filePath.asStringRef(), alreadyUsedFiles[filePath].asStringRef(),
              tuPath.asStringRef());
      alreadyUsedFiles.insert({filePath, tuPath});
    }

    this->checkOrUpdate(mode, std::move(output), inputToOutputMap);
  });
}

void MultiTuSnapshotTest::runWithMerging(
    SnapshotMode mode, RunMultiTuCompileCommandCallback compute) {
  std::vector<CommandObjectBuilder> compdbBuilders{};

  this->iterateOverTus([&](CommandObjectBuilder &&entryBuilder) {
    compdbBuilders.emplace_back(std::move(entryBuilder));
  });

  auto output = compute(this->rootPath,
                        CompilationDatabaseBuilder{std::move(compdbBuilders)});

  this->checkOrUpdate(mode, std::move(output.snapshots),
                      this->buildInputToOutputMap());

  auto absPath = rootPath.makeAbsolute(RootRelativePathRef{
      MultiTuSnapshotTest::externalSymbolsSnapshotPath, RootKind::Project});
  if (!std::filesystem::exists(absPath.asStringRef())) {
    return;
  }
  auto snapshot =
      SnapshotPrinter::formatExternalSymbols(std::move(output.externalSymbols));
  test::compareOrUpdateSingleFile(mode, snapshot, absPath.asStringRef());
}

MultiTuSnapshotTest::InputToOutputMap
MultiTuSnapshotTest::buildInputToOutputMap() {
  absl::flat_hash_map<RootRelativePathRef, RootRelativePathRef>
      inputToOutputMap;
  for (auto &io : this->inputOutputs) {
    inputToOutputMap.insert(
        {io.sourceFilePath.asRef(), io.snapshotPath.asRef()});
  }
  return inputToOutputMap;
}

void MultiTuSnapshotTest::iterateOverTus(PerTuCallback perTuCallback) {
  size_t index = 0;
  for (auto &io : this->inputOutputs) {
    auto &sourceFileRelPath = io.sourceFilePath.asStringRef();
    if (!test::isTuMainFilePath(sourceFileRelPath)) {
      continue;
    }
    std::vector<std::string> commandLine{"clang", "-I", ".", sourceFileRelPath};
    {
      auto sourceFileAbsPath =
          this->rootPath.makeAbsolute(io.sourceFilePath.asRef());
      ENFORCE(std::filesystem::exists(sourceFileAbsPath.asStringRef()));
      std::ifstream tuStream(sourceFileAbsPath.asStringRef(),
                             std::ios::in | std::ios::binary);
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
    perTuCallback(CommandObjectBuilder{index, io.sourceFilePath.asRef(),
                                       std::move(commandLine)});
    ++index;
  }
}

void MultiTuSnapshotTest::checkOrUpdate(
    SnapshotMode mode, SnapshotContentsMap &&output,
    const InputToOutputMap &inputToOutputMap) {
  for (auto &[inputPath, _] : inputToOutputMap) {
    if (!test::isTuMainFilePath(inputPath.asStringView())) {
      continue;
    }
    if (inputPath.asStringView().find("external") == std::string::npos) {
      auto it = output.find(RootRelativePath{inputPath});
      ENFORCE(it != output.end(), "snapshot missing file for {}",
              inputPath.asStringView());
    }
  }
  scip_clang::extractTransform(
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

} // namespace test
} // namespace scip_clang
