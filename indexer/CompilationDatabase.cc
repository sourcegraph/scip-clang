#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "indexer/Enforce.h" // Defines ENFORCE used by rapidjson headers
#include "indexer/Path.h"

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/strip.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/search_path.hpp"
#include "rapidjson/error/en.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/reader.h"
#include "spdlog/fmt/fmt.h"

#include "llvm/Support/Regex.h"

#include "indexer/CompilationDatabase.h"
#include "indexer/FileSystem.h"
#include "indexer/LlvmCommandLineParsing.h"

namespace {
enum class CompilerKind {
  Gcc,
  Clang,
};

struct CompletedProcess {
  int exitCode;
  std::optional<boost::process::process_error> error;
  std::vector<std::string> stdoutLines;
  std::vector<std::string> stderrLines;

  bool isSuccess() const {
    return this->exitCode == EXIT_SUCCESS && !this->error.has_value();
  }
};

struct ResourceDirResult {
  std::string resourceDir;
  std::vector<std::string> cliInvocation;
  CompilerKind compilerKind;
};
} // namespace

static CompletedProcess runProcess(std::vector<std::string> &args,
                                   const char *logContext) {
  CompletedProcess out{.exitCode = EXIT_FAILURE,
                       .error = std::nullopt,
                       .stdoutLines = {},
                       .stderrLines = {}};
  boost::process::ipstream stdoutStream, stderrStream;
  BOOST_TRY {
    spdlog::debug("{}{}invoking '{}'", logContext ? logContext : "",
                  logContext ? " by " : "", fmt::join(args, " "));
    boost::process::child worker(args, boost::process::std_out > stdoutStream,
                                 boost::process::std_err > stderrStream);
    worker.wait();
    out.exitCode = worker.exit_code();
  }
  BOOST_CATCH(boost::process::process_error & ex) {
    out.error = ex;
  }
  BOOST_CATCH_END
  std::string line;
  while (std::getline(stdoutStream, line) && !line.empty()) {
    out.stdoutLines.push_back(line);
  }
  while (std::getline(stderrStream, line) && !line.empty()) {
    out.stderrLines.push_back(line);
  }
  return out;
}

/// Returns an empty path if we failed to determine the resource dir
ResourceDirResult static determineResourceDir(const std::string &compilerPath) {
  ResourceDirResult out{
      "", {compilerPath, "-print-resource-dir"}, CompilerKind::Clang};
  // std::pair<CompilerKind, std::string> errorResult = {CompilerKind::Clang,
  // ""}; std::vector<std::string> args = {compilerPath,
  //                                  "-print-resource-dir"};
  auto printResourceDirResult =
      ::runProcess(out.cliInvocation, "attempting to find resource dir");
  if (printResourceDirResult.isSuccess()) {
    if (printResourceDirResult.stdoutLines.empty()) {
      spdlog::warn(
          "-print-resource-dir succeeded but returned an empty result");
      return out;
    }
    out.resourceDir = std::string(
        absl::StripAsciiWhitespace(printResourceDirResult.stdoutLines.front()));
    return out;
  }
  out.compilerKind = CompilerKind::Gcc;
  out.cliInvocation = {compilerPath, "-print-search-dirs"};
  auto printSearchDirsResult =
      ::runProcess(out.cliInvocation, "attempting to find search dirs");
  if (!printSearchDirsResult.isSuccess()) {
    spdlog::warn(
        "both -print-resource-dir and -print-search-dirs failed for compiler "
        "{}; may be unable to locate standard library headers",
        compilerPath);
    return out;
  }
  absl::c_any_of(
      printSearchDirsResult.stdoutLines, [&](const std::string &line) -> bool {
        if (line.starts_with("install:")) {
          out.resourceDir =
              absl::StripAsciiWhitespace(absl::StripPrefix(line, "install:"));
          return true;
        }
        return false;
      });
  if (out.resourceDir.empty()) {
    spdlog::warn(
        "missing 'install:' line in -print-search-dirs from GCC(-like?) "
        "compiler {}; may be unable to locate standard library headers",
        compilerPath);
    return out;
  }
  return out;
}

namespace scip_clang {
namespace compdb {
namespace {

// Handler to validate a compilation database in a streaming fashion.
//
// Spec: https://clang.llvm.org/docs/JSONCompilationDatabase.html
template <typename H>
class ValidateHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                          ValidateHandler<H>> {
  H &inner;

  enum class Context {
    // Outside the outermost '['
    Outermost,
    // Inside the outermost '[' but outside any command object
    InTopLevelArray,
    // Inside a '{'
    InObject,
    // At the RHS after seeing "arguments"
    InArgumentsValue,
    // Inside the array after "arguments": [
    InArgumentsValueArray,
  } context;

  uint32_t presentKeys;
  Key lastKey;
  ValidationOptions options;

public:
  std::string errorMessage;
  absl::flat_hash_set<std::string> warnings;

  ValidateHandler(H &inner, ValidationOptions options)
      : inner(inner), context(Context::Outermost), lastKey(Key::Unset),
        options(options), errorMessage(), warnings() {}

private:
  void markContextIllegal(std::string forItem) {
    const char *ctx;
    switch (this->context) {
    case Context::Outermost:
      ctx = "outermost context";
      break;
    case Context::InTopLevelArray:
      ctx = "top-level array context";
      break;
    case Context::InObject:
      ctx = "command object context";
      break;
    case Context::InArgumentsValue:
      ctx = "value for the key 'arguments'";
      break;
    case Context::InArgumentsValueArray:
      ctx = "array for the key 'arguments'";
      break;
    }
    this->errorMessage = fmt::format("unexpected {} in {}", forItem, ctx);
  }

  std::optional<std::string> checkNecessaryKeysPresent() const {
    std::vector<std::string> missingKeys;
    using UInt = decltype(this->presentKeys);
    if (!(this->presentKeys & UInt(Key::Directory))) {
      missingKeys.push_back("directory");
    }
    if (!(this->presentKeys & UInt(Key::File))) {
      missingKeys.push_back("file");
    }
    if (!(this->presentKeys & UInt(Key::Command))
        && !(this->presentKeys & UInt(Key::Arguments))) {
      missingKeys.push_back("either command or arguments");
    }
    if (missingKeys.empty()) {
      return {};
    }
    std::string buf;
    for (size_t i = 0; i < missingKeys.size() - 1; i++) {
      buf.append(missingKeys[i]);
      buf.append(", ");
    }
    buf.append(" and ");
    buf.append(missingKeys.back());
    return buf;
  }

public:
  bool Null() {
    this->errorMessage = "unexpected null";
    return false;
  }
  bool Bool(bool b) {
    this->errorMessage = fmt::format("unexpected bool {}", b);
    return false;
  }
  bool Int(int i) {
    this->errorMessage = fmt::format("unexpected int {}", i);
    return false;
  }
  bool Uint(unsigned i) {
    this->errorMessage = fmt::format("unexpected unsigned int {}", i);
    return false;
  }
  bool Int64(int64_t i) {
    this->errorMessage = fmt::format("unexpected int64_t {}", i);
    return false;
  }
  bool Uint64(uint64_t i) {
    this->errorMessage = fmt::format("unexpected uint64_t {}", i);
    return false;
  }
  bool Double(double d) {
    this->errorMessage = fmt::format("unexpected double {}", d);
    return false;
  }
  bool RawNumber(const char *str, rapidjson::SizeType length, bool /*copy*/) {
    this->errorMessage =
        fmt::format("unexpected number {}", std::string_view(str, length));
    return false;
  }
  bool String(const char *str, rapidjson::SizeType length, bool copy) {
    switch (this->context) {
    case Context::Outermost:
    case Context::InTopLevelArray:
    case Context::InArgumentsValue:
      this->markContextIllegal("string");
      return false;
    case Context::InObject:
      if (this->options.checkDirectoryPathsAreAbsolute
          && this->lastKey == Key::Directory) {
        auto dirPath = std::string_view(str, length);
        // NOTE(ref: directory-field-is-absolute): While the JSON compilation
        // database schema
        // (https://clang.llvm.org/docs/JSONCompilationDatabase.html) does not
        // specify if the "directory" key should be an absolute path or not, if
        // it is relative, it is ambiguous as to which directory should be used
        // as the root if it is relative (the directory containing the
        // compile_commands.json is one option).
        if (!AbsolutePathRef::tryFrom(dirPath).has_value()) {
          this->errorMessage = fmt::format(
              "expected absolute path for \"directory\" key but found '{}'",
              dirPath);
          return false;
        }
      }
      return this->inner.String(str, length, copy);
    case Context::InArgumentsValueArray:
      return this->inner.String(str, length, copy);
    }
  }
  bool StartObject() {
    switch (this->context) {
    case Context::Outermost:
    case Context::InObject:
    case Context::InArgumentsValue:
    case Context::InArgumentsValueArray:
      this->markContextIllegal("object start ('{')");
      return false;
    case Context::InTopLevelArray:
      this->context = Context::InObject;
      return this->inner.StartObject();
    }
  }
  bool Key(const char *str, rapidjson::SizeType length, bool copy) {
    switch (this->context) {
    case Context::Outermost:
    case Context::InTopLevelArray:
    case Context::InArgumentsValue:
    case Context::InArgumentsValueArray:
      this->markContextIllegal(
          fmt::format("object key {}", std::string_view(str, length)));
      return false;
    case Context::InObject:
      auto key = std::string_view(str, length);
      using UInt = decltype(this->presentKeys);
      compdb::Key sawKey = Key::Unset;
      if (key == "directory") {
        sawKey = Key::Directory;
      } else if (key == "file") {
        sawKey = Key::File;
      } else if (key == "command") {
        sawKey = Key::Command;
      } else if (key == "arguments") {
        this->context = Context::InArgumentsValue;
        sawKey = Key::Arguments;
      } else if (key == "output") {
        sawKey = Key::Output;
      } else {
        this->warnings.insert(fmt::format("unknown key {}", key));
      }
      if (sawKey != Key::Unset) {
        this->lastKey = sawKey;
        this->presentKeys |= UInt(this->lastKey);
      }
      return this->inner.Key(str, length, copy);
    }
  }
  bool EndObject(rapidjson::SizeType memberCount) {
    switch (this->context) {
    case Context::Outermost:
    case Context::InTopLevelArray:
    case Context::InArgumentsValue:
    case Context::InArgumentsValueArray:
      this->markContextIllegal("object end ('}')");
      return false;
    case Context::InObject:
      this->context = Context::InTopLevelArray;
      if (auto missing = this->checkNecessaryKeysPresent()) {
        spdlog::warn("missing keys: {}", missing.value());
      }
      this->presentKeys = 0;
      return this->inner.EndObject(memberCount);
    }
  }
  bool StartArray() {
    switch (this->context) {
    case Context::InTopLevelArray:
    case Context::InObject:
    case Context::InArgumentsValueArray:
      this->markContextIllegal("array start ('[')");
      return false;
    case Context::Outermost:
      this->context = Context::InTopLevelArray;
      break;
    case Context::InArgumentsValue:
      this->context = Context::InArgumentsValueArray;
      break;
    }
    return this->inner.StartObject();
  }
  bool EndArray(rapidjson::SizeType elementCount) {
    switch (this->context) {
    case Context::Outermost:
    case Context::InObject:
    case Context::InArgumentsValue:
      this->markContextIllegal("array end (']')");
      return false;
    case Context::InTopLevelArray:
      this->context = Context::Outermost;
      break;
    case Context::InArgumentsValueArray:
      this->context = Context::InObject;
      break;
    }
    return this->inner.EndArray(elementCount);
  }
};

} // namespace

// Validates a compilation database, counting the number of jobs along the
// way to allow for better planning.
//
// Uses the global logger and exits if the compilation database is invalid.
//
// Returns the number of jobs in the database.
static size_t validateAndCountJobs(size_t fileSize, FILE *compDbFile,
                                   ValidationOptions validationOptions) {
  struct ArrayCountHandler
      : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                            ArrayCountHandler> {
    // A single count is sufficient instead of a stack because this field is
    // only read if the database is valid, which means we'll end up with the
    // count for the outermost array (which contains command objects).
    size_t count = 0;
    bool EndArray(rapidjson::SizeType count) {
      this->count = count;
      return true;
    };
  };
  rapidjson::Reader reader;
  ArrayCountHandler countHandler;
  ValidateHandler<ArrayCountHandler> validator(countHandler, validationOptions);
  std::string buffer(std::min(size_t(1024 * 1024), fileSize), 0);
  auto stream =
      rapidjson::FileReadStream(compDbFile, buffer.data(), buffer.size());
  auto parseResult = reader.Parse(stream, validator);
  if (parseResult.IsError()) {
    spdlog::error("failed to parse compile_commands.json: {}",
                  validator.errorMessage);
    std::exit(EXIT_FAILURE);
  }
  if (!validator.warnings.empty()) {
    std::vector<std::string> warnings(validator.warnings.begin(),
                                      validator.warnings.end());
    absl::c_sort(warnings);
    for (auto &warning : warnings) {
      spdlog::warn("in compile_commands.json: {}", warning);
    }
  }
  return countHandler.count;
}

bool CommandObjectHandler::String(const char *str, rapidjson::SizeType length,
                                  bool /*copy*/) {
  switch (this->previousKey) {
  case Key::Unset:
    ENFORCE(false, "unexpected input");
    return false;
  case Key::Directory:
    this->wipCommand.Directory = std::string(str, length);
    break;
  case Key::File:
    this->wipCommand.Filename = std::string(str, length);
    break;
  case Key::Command:
    this->wipCommand.CommandLine = scip_clang::unescapeCommandLine(
        clang::tooling::JSONCommandLineSyntax::AutoDetect,
        std::string_view(str, length));
    break;
  case Key::Arguments: // Validator makes sure we have an array outside.
    this->wipCommand.CommandLine.emplace_back(str, length);
    break;
  case Key::Output:
    this->wipCommand.Output = std::string(str, length);
    break;
  }
  return true;
}

bool CommandObjectHandler::Key(const char *str, rapidjson::SizeType length,
                               bool /*copy*/) {
  auto key = std::string_view(str, length);
  if (key == "directory") {
    this->previousKey = Key::Directory;
  } else if (key == "file") {
    this->previousKey = Key::File;
  } else if (key == "command") {
    this->previousKey = Key::Command;
  } else if (key == "output") {
    this->previousKey = Key::Output;
  } else if (key == "arguments") {
    this->previousKey = Key::Arguments;
  } else {
    ENFORCE(false, "unexpected key should've been caught by validation");
  }
  return true;
}

bool CommandObjectHandler::EndObject(rapidjson::SizeType /*memberCount*/) {
  this->commands.emplace_back(std::move(this->wipCommand));
  this->wipCommand = {};
  this->previousKey = Key::Unset;
  return true;
}

bool CommandObjectHandler::reachedLimit() const {
  return this->commands.size() == this->parseLimit;
}

CompilationDatabaseFile
CompilationDatabaseFile::open(const StdPath &path,
                              ValidationOptions validationOptions,
                              std::error_code &fileSizeError) {
  CompilationDatabaseFile compdbFile{};
  compdbFile.file = std::fopen(path.c_str(), "rb");
  if (!compdbFile.file) {
    return compdbFile;
  }
  auto size = std::filesystem::file_size(path, fileSizeError);
  if (fileSizeError) {
    return compdbFile;
  }
  compdbFile._sizeInBytes = size;
  compdbFile._commandCount = validateAndCountJobs(
      compdbFile._sizeInBytes, compdbFile.file, validationOptions);
  return compdbFile;
}

CompilationDatabaseFile CompilationDatabaseFile::openAndExitOnErrors(
    const StdPath &path, ValidationOptions validationOptions) {
  std::error_code fileSizeError;
  auto compdbFile =
      CompilationDatabaseFile::open(path, validationOptions, fileSizeError);
  if (!compdbFile.file) {
    spdlog::error("failed to open '{}': {}", path.string(),
                  std::strerror(errno));
    std::exit(EXIT_FAILURE);
  }
  if (fileSizeError) {
    spdlog::error("failed to read file size for '{}': {}", path.string(),
                  fileSizeError.message());
    std::exit(EXIT_FAILURE);
  }
  if (compdbFile.commandCount() == 0) {
    spdlog::error("compile_commands.json has 0 objects in outermost array; "
                  "nothing to index");
    std::exit(EXIT_FAILURE);
  }
  return compdbFile;
}

void ResumableParser::initialize(CompilationDatabaseFile compdb,
                                 size_t refillCount, bool inferResourceDir) {
  auto averageJobSize = compdb.sizeInBytes() / compdb.commandCount();
  // Some customers have averageJobSize = 150KiB.
  // If numWorkers == 300 (very high core count machine),
  // then the computed hint will be ~88MiB. The 128MiB is rounded up from 88MiB.
  // The fudge factor of 2 is to allow for oversized jobs.
  auto bufferSize =
      std::min(size_t(128 * 1024 * 1024), averageJobSize * 2 * refillCount);
  std::fseek(compdb.file, 0, SEEK_SET);
  this->handler = CommandObjectHandler(refillCount);
  this->jsonStreamBuffer.resize(bufferSize);
  this->compDbStream =
      rapidjson::FileReadStream(compdb.file, this->jsonStreamBuffer.data(),
                                this->jsonStreamBuffer.size());
  this->reader.IterativeParseInit();
  this->inferResourceDir = inferResourceDir;
}

void ResumableParser::parseMore(
    std::vector<clang::tooling::CompileCommand> &out) {
  if (this->reader.IterativeParseComplete()) {
    if (this->reader.HasParseError()) {
      spdlog::error(
          "parse error: {} at offset {}",
          rapidjson::GetParseError_En(this->reader.GetParseErrorCode()),
          this->reader.GetErrorOffset());
    }
    return;
  }
  ENFORCE(this->handler, "should've been handled by initializer method");
  ENFORCE(this->compDbStream, "should've been handled by initializer method");

  while (!this->handler->reachedLimit()
         && !this->reader.IterativeParseComplete()) {
    this->reader.IterativeParseNext<rapidjson::kParseIterativeFlag>(
        this->compDbStream.value(), this->handler.value());
  }
  for (auto &cmd : this->handler->commands) {
    out.emplace_back(std::move(cmd));
  }
  if (this->inferResourceDir) {
    for (auto &cmd : out) {
      if (cmd.CommandLine.empty()) {
        continue;
      }
      this->tryInferResourceDir(cmd.CommandLine);
    }
  }
  this->handler->commands.clear();
}

void ResumableParser::tryInferResourceDir(
    std::vector<std::string> &commandLine) {
  auto &compilerPath = commandLine.front();
  auto it = this->extraArgsMap.find(compilerPath);
  if (it != this->extraArgsMap.end()) {
    for (auto &extraArg : it->second) {
      commandLine.push_back(extraArg);
    }
    return;
  }
  std::string compilerInvocationPath = compilerPath;
  if (compilerPath.find(std::filesystem::path::preferred_separator)
      == std::string::npos) {
    compilerInvocationPath = boost::process::search_path(compilerPath).native();
    if (compilerInvocationPath.empty()) {
      this->emitResourceDirError(fmt::format(
          "scip-clang needs to be invoke '{0}' (found via the compilation"
          " database) to determine the resource directory, but couldn't find"
          " '{0}' on PATH. Hint: Use a modified PATH to invoke scip-clang,"
          " or change the compilation database to use absolute paths"
          " for the compiler.",
          compilerPath));
      return;
    }
  }
  auto fail = [&]() { this->extraArgsMap.insert({compilerPath, {}}); };
  auto resourceDirResult = ::determineResourceDir(compilerInvocationPath);
  if (resourceDirResult.resourceDir.empty()) {
    return fail();
  }
  auto &resourceDir = resourceDirResult.resourceDir;
  std::vector<std::string> extraArgs{"-resource-dir", resourceDir};
  if (resourceDirResult.compilerKind == CompilerKind::Gcc) {
    extraArgs.push_back(fmt::format("-I{}/include-fixed", resourceDir));
  }
  spdlog::debug("got resource dir '{}'", resourceDir);
  if (!std::filesystem::exists(resourceDir)) {
    this->emitResourceDirError(fmt::format(
        "'{}' returned '{}' but the directory does not exist",
        fmt::join(resourceDirResult.cliInvocation, " "), resourceDir));
    return fail();
  }
  auto [newIt, inserted] =
      this->extraArgsMap.emplace(compilerPath, std::move(extraArgs));
  ENFORCE(inserted);
  for (auto &arg : newIt->second) {
    commandLine.push_back(arg);
  }
}

void ResumableParser::emitResourceDirError(std::string &&error) {
  auto [it, inserted] = this->emittedErrors.emplace(std::move(error));
  if (inserted) {
    spdlog::error("{}", *it);
  }
}

} // namespace compdb
} // namespace scip_clang