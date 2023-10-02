#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "indexer/Enforce.h" // Defines ENFORCE used by rapidjson headers
#include "indexer/Path.h"

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
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

#include "indexer/CompilationDatabase.h"
#include "indexer/FileSystem.h"
#include "indexer/LlvmCommandLineParsing.h"

namespace {

struct CompletedProcess {
  int exitCode;
  std::optional<boost::process::process_error> error;
  std::vector<std::string> stdoutLines;
  std::vector<std::string> stderrLines;

  bool isSuccess() const {
    return this->exitCode == EXIT_SUCCESS && !this->error.has_value();
  }
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

namespace {

using AbsolutePath = scip_clang::AbsolutePath;
using ToolchainInfo = scip_clang::compdb::ToolchainInfo;
using CompilerKind = scip_clang::compdb::CompilerKind;

struct ClangToolchainInfo : public ToolchainInfo {
  std::string resourceDir;
  std::vector<std::string> findResourceDirInvocation;
  std::string compilerDriverPath;
  std::vector<std::string> findDriverInvocation;

  // All strings and vectors above should be non-empty for
  // a valid toolchain.

  ClangToolchainInfo(std::string resourceDir,
                     std::vector<std::string> findResourceDirInvocation,
                     std::string compilerDriverPath,
                     std::vector<std::string> findDriverInvocation)
      : ToolchainInfo(), resourceDir(resourceDir),
        findResourceDirInvocation(findResourceDirInvocation),
        compilerDriverPath(compilerDriverPath),
        findDriverInvocation(findDriverInvocation){};

  virtual CompilerKind kind() const override {
    return CompilerKind::Clang;
  }

  virtual bool isWellFormed() const override {
    if (!std::filesystem::exists(this->resourceDir)) {
      spdlog::error(
          "clang resource directory '{}' does not exist (obtained via {})",
          this->resourceDir, fmt::join(this->findResourceDirInvocation, " "));
      return false;
    }
    if (!std::filesystem::exists(this->compilerDriverPath)) {
      spdlog::error("compiler driver at '{}' does not exist (obtained via {})",
                    this->compilerDriverPath,
                    fmt::join(this->findDriverInvocation, " "));
      return false;
    }
    return true;
  }

  virtual void
  adjustCommandLine(std::vector<std::string> &commandLine) const override {
    commandLine[0] = this->compilerDriverPath;
    commandLine.push_back("-resource-dir");
    commandLine.push_back(this->resourceDir);
  }

  static std::unique_ptr<ClangToolchainInfo>
  tryInfer(const AbsolutePath &compilerPath) {
    std::vector<std::string> findResourceDirInvocation = {
        compilerPath.asStringRef(), "-print-resource-dir"};
    auto printResourceDirResult = ::runProcess(
        findResourceDirInvocation, "attempting to find resource dir");
    if (!printResourceDirResult.isSuccess()) {
      return nullptr;
    }
    if (printResourceDirResult.stdoutLines.empty()) {
      spdlog::warn("{} succeeded but returned an empty result",
                   fmt::join(findResourceDirInvocation, " "));
      return nullptr;
    }
    auto resourceDir = std::string(
        absl::StripAsciiWhitespace(printResourceDirResult.stdoutLines.front()));
    spdlog::debug("got resource dir {} from {}", resourceDir,
                  compilerPath.asStringRef());

    std::vector<std::string> findDriverInvocation = {compilerPath.asStringRef(),
                                                     "-###"};
    auto hashHashHashResult = ::runProcess(
        findDriverInvocation, "attempting to find installed directory");
    std::string compilerDriverPath = "";
    if (hashHashHashResult.isSuccess()) {
      for (auto &line : hashHashHashResult.stderrLines) {
        auto clangDriverDir = absl::StripPrefix(line, "InstalledDir: ");
        if (clangDriverDir.length() != line.length()) {
          compilerDriverPath = scip_clang::joinPath(
              absl::StripAsciiWhitespace(clangDriverDir), "clang");
          spdlog::debug("found compiler driver at {}", compilerDriverPath);
          break;
        }
      }
    }
    if (compilerDriverPath.empty()) {
      spdlog::warn(
          "failed to determine compiler path using -### for compiler at '{}'",
          compilerPath.asStringRef());
      ToolchainInfo::logStdlibWarning();
      return nullptr;
    }

    return std::make_unique<ClangToolchainInfo>(
        resourceDir, findResourceDirInvocation, compilerDriverPath,
        findDriverInvocation);
  }
};

struct GccToolchainInfo : public ToolchainInfo {
  std::string installDir;
  std::vector<std::string> findInstallDirInvocation;

  GccToolchainInfo(std::string installDir,
                   std::vector<std::string> findInstallDirInvocation)
      : ToolchainInfo(), installDir(installDir),
        findInstallDirInvocation(findInstallDirInvocation) {}

  virtual CompilerKind kind() const override {
    return CompilerKind::Gcc;
  }

  virtual bool isWellFormed() const override {
    if (!std::filesystem::exists(this->installDir)) {
      spdlog::error(
          "GCC install directory '{}' does not exist (obtained via {})",
          this->installDir, fmt::join(this->findInstallDirInvocation, " "));
      return false;
    }
    return true;
  }

  virtual void
  adjustCommandLine(std::vector<std::string> &commandLine) const override {
    commandLine.push_back("-resource-dir");
    commandLine.push_back(this->installDir);
    // gcc-7 adds headers like limits.h and syslimits.h in include-fixed
    commandLine.push_back(fmt::format("-I{}/include-fixed", this->installDir));
  }

  static std::unique_ptr<GccToolchainInfo>
  tryInfer(const AbsolutePath &compilerPath) {
    std::vector<std::string> findSearchDirsInvocation = {
        compilerPath.asStringRef(), "-print-search-dirs"};
    auto printSearchDirsResult = ::runProcess(findSearchDirsInvocation,
                                              "attempting to find search dirs");
    if (!printSearchDirsResult.isSuccess()) {
      return nullptr;
    }
    std::string installDir;
    absl::c_any_of(printSearchDirsResult.stdoutLines,
                   [&](const std::string &line) -> bool {
                     if (line.starts_with("install:")) {
                       installDir = absl::StripAsciiWhitespace(
                           absl::StripPrefix(line, "install:"));
                       return true;
                     }
                     return false;
                   });
    if (installDir.empty()) {
      spdlog::warn(
          "missing 'install:' line in -print-search-dirs from GCC(-like?) {}",
          compilerPath.asStringRef());
      ToolchainInfo::logStdlibWarning();
      return nullptr;
    }
    spdlog::debug("found gcc install directory at {}", installDir);
    return std::make_unique<GccToolchainInfo>(installDir,
                                              findSearchDirsInvocation);
  }
};

enum class NvccOptionType {
  NoArgument,
  OneArgument,
};

// Based on nvcc --help from nvcc version V12.2.140
// Build cuda_12.2.r12.2/compiler.33191640_0

// clang-format off
constexpr const char* skipOptionsNoArgs[] = {
  "--cuda", "-cuda",
  "--cubin", "-cubin",
  "--fatbin", "-fatbin",
  "--ptx", "-ptx",
  "--optix-ir", "-optix-ir",
  "--generate-dependencies", // clang uses --dependencies,
  "--compile",
  "--device-c", "-dc",
  "--device-w", "-dw",
  "--device-link", "-dlink",
  "--link", "-link",
  "--lib", "-lib",
  "--run", "-run",
  "--output-file", // clang uses --output
  "--compiler-bindir", "-ccbin",
  "--allow-unsupported-compiler",
  "--archiver-binary", "-arbin",
  "--use-local-env", "-use-local-env",
  "--profile", "-pg",
  "--debug",
  "--device-debug", "-G",
  "--generate-line-info",
  "--dlink-time-opt", "-dlto",
  "--gen-opt-lto", "-gen-opt-lto",
  "--no-host-device-initializer-list", "-nohdinitlist",
  "--no-host-device-move-forward", "-nohdmvforward",
  "--expt-relaxed-constexpr", "-expt-relaxed-constexpr",
  "--extended-lambda", "-extended-lambda",
  "--expt-extended-lambda", "-expt-extended-lambda",
  "--m64", "-m64",
  "--forward-unknown-to-host-compiler", "-forward-unknown-to-host-compiler",
  "--forward-unknown-opts", "-forward-unknown-opts",
  "--keep", "-keep",
  "--save-temps", "-save-temps",
  "--no-align-double", "-no-align-double",
  "--no-device-link", "-nodlink",
  "--extra-device-vectorization", "-extra-device-vectorization",
  "--disable-warnings", "-w",
  "--keep-device-functions", "-keep-device-functions",
  "--source-in-ptx", "-src-in-ptx",
  "--restrict", "-restrict",
  "--Wreorder", "-Wreorder",
  "--Wdefault-stream-launch", "-Wdefault-stream-launch",
  "--Wmissing-launch-bounds", "-Wmissing-launch-bounds",
  "--Wext-lambda-captures-this", "-Wext-lambda-captures-this",
  "--Wno-deprecated-declarations", "-Wno-deprecated-declarations",
  "--Wno-deprecated-gpu-targets", "-Wno-deprecated-gpu-targets",
  "--resource-usage", "-res-usage",
  "--extensible-whole-program", "-ewp",
  "--no-compress", "-no-compress",
  "--qpp-config", "-qpp-config",
  "--compile-as-tools-patch", "-astoolspatch",
  "--display-error-number", "-err-no",
  "--no-display-error-number", "-no-err-no",
  "--augment-host-linker-script", "-aug-hls",
  "--host-relocatable-link", "-r"
};

constexpr const char* skipOptionsWithArgs[] = {
  "--cudart", "-cudart",
  "--cudadevrt", "-cudadevrt",
  "--libdevice-directory", "-ldir",
  "--target-directory", "-target-dir",
  "--optimization-info",
  "--optimize",
  "--dopt", "-dopt",
  "--machine", "-m",
  "--threads", "-t",
  "--split-compile", "-split-compile",
  "--keep-dir", "-keep-dir",
  "--linker-options",
  "--archive-options", "-Xarchive",
  "--ptxas-options", "-Xptxas",
  "--nvlink-options", "-Xnvlink",
  "--time", "-time",
  "--run-args", "-run-args",
  "--input-drive-prefix", "-idp",
  "--dependency-drive-prefix", "-ddp",
  "--drive-prefix", "-dp",
  "-dependency-target-name",
  "--gpu-architecture",
  "--gpu-code", "-code",
  "--generate-code", "-gencode",
  "--relocatable-device-code", "-rdc",
  "--entries", "-e",
  "--maxrregcount", "-maxrregcount",
  "--use_fast_math", "-use_fast_math",
  "--ftz", "-ftz",
  "--prec-div", "-prec-div",
  "--prec-sqrt", "-prec-sqrt",
  "--fmad", "-fmad",
  "--default-stream", "-default-stream",
  "--Werror", "-Werror",
  "--diag-error", "-diag-error",
  "--diag-suppress", "-diag-suppress",
  "--diag-warn", "-diag-warn",
  "--host-linker-script", "-hls",
  "--brief-diagnostics", "-brief-diag"
};

// clang-format on

struct NvccToolchainInfo : public ToolchainInfo {
  AbsolutePath cudaDir;

  /// Identify where the clang toolchain is based on PATH, if possible.
  /// Without the appropriate Clang headers, it seems like the frontend
  /// doesn't even construct the appropriate CUDAKernelCallExpr values.
  std::unique_ptr<ClangToolchainInfo> clangInfo;

  absl::flat_hash_map<std::string_view, NvccOptionType> toBeSkipped;

  NvccToolchainInfo(AbsolutePath cudaDir)
      : ToolchainInfo(), cudaDir(cudaDir), clangInfo(nullptr) {
    for (auto s : skipOptionsNoArgs) {
      this->toBeSkipped.emplace(std::string_view(s),
                                NvccOptionType::NoArgument);
    }
    for (auto s : skipOptionsWithArgs) {
      this->toBeSkipped.emplace(std::string_view(s),
                                NvccOptionType::OneArgument);
    }

    // TODO: In principle, we could pick up Clang from -ccbin but that
    // requires more plumbing; it would require using the -ccbin arg
    // as part of the hash map key for toolchainInfoMap. So instead,
    // for now, just require that the same Clang be available on PATH.
    auto clangPath = boost::process::search_path("clang").native();
    if (!clangPath.empty()) {
      auto clangAbsPath =
          AbsolutePath(std::string(clangPath.data(), clangPath.size()));
      this->clangInfo = ClangToolchainInfo::tryInfer(clangAbsPath);
    }
    if (clangInfo) {
      return;
    }
    spdlog::error("clang not found on PATH; may be unable to locate headers "
                  "like __clang_cuda_runtime_wrapper.h");
    spdlog::warn("code navigation for kernel call expressions may not work in "
                 "the absence of Clang CUDA headers");
    ToolchainInfo::logStdlibWarning();
  }

  virtual CompilerKind kind() const override {
    return CompilerKind::Nvcc;
  }

  virtual bool isWellFormed() const override {
    auto path = scip_clang::joinPath(cudaDir.asStringRef(), "include");
    if (!std::filesystem::exists(path)) {
      spdlog::error(
          "directory '{}' does not exist; expected to find CUDA SDK headers"
          " there because nvcc was found at {}",
          path,
          scip_clang::joinPath(cudaDir.asStringRef(),
                               scip_clang::joinPath("bin", "nvcc")));
      return false;
    }
    return true;
  }

  enum class ArgumentProcessing {
    Keep,
    DropCurrent,
    DropCurrentAndNextIffBothPresent,
  };

  ArgumentProcessing handleArgument(const std::string &arg) const {
    if (!arg.starts_with('-')) {
      return ArgumentProcessing::Keep;
    }
    std::string_view substr = arg;
    auto eqIndex = arg.find('=');
    if (eqIndex != std::string::npos) {
      substr = std::string_view(arg.data(), eqIndex);
    }
    auto it = this->toBeSkipped.find(substr);
    if (it == this->toBeSkipped.end()) {
      return ArgumentProcessing::Keep;
    }
    switch (it->second) {
    case NvccOptionType::NoArgument:
      return ArgumentProcessing::DropCurrent;
    case NvccOptionType::OneArgument:
      if (substr.size() == arg.size()) {
        return ArgumentProcessing::DropCurrentAndNextIffBothPresent;
      }
      return ArgumentProcessing::DropCurrent;
    }
    ENFORCE(false, "should've exited earlier");
  }

  void removeUnknownArguments(std::vector<std::string> &commandLine) const {
    // TODO: Add special handling for --compiler-options, -Xcompiler
    // and --options-file.
    absl::flat_hash_set<size_t> drop{};
    for (size_t i = 0; i < commandLine.size(); ++i) {
      switch (this->handleArgument(commandLine[i])) {
      case ArgumentProcessing::Keep:
        continue;
      case ArgumentProcessing::DropCurrent:
        drop.insert(i);
        continue;
      case ArgumentProcessing::DropCurrentAndNextIffBothPresent:
        if (i + 1 < commandLine.size()) {
          drop.insert(i);
          drop.insert(i + 1);
        }
      }
    }
    std::vector<std::string> tmp;
    tmp.reserve(commandLine.size() - drop.size());
    for (size_t i = 0; i < commandLine.size(); ++i) {
      if (!drop.contains(i)) {
        tmp.push_back(std::move(commandLine[i]));
      }
    }
    std::swap(tmp, commandLine);
  }

  virtual void
  adjustCommandLine(std::vector<std::string> &commandLine) const override {
    this->removeUnknownArguments(commandLine);
    commandLine.push_back(
        fmt::format("-isystem{}{}include", this->cudaDir.asStringRef(),
                    std::filesystem::path::preferred_separator));
    if (this->clangInfo) {
      this->clangInfo->adjustCommandLine(commandLine);
    }
  }

  static std::unique_ptr<NvccToolchainInfo>
  tryInfer(const AbsolutePath &compilerPath) {
    std::vector<std::string> argv = {compilerPath.asStringRef(), "--version"};
    auto compilerVersionResult = ::runProcess(argv, "checking for NVCC");
    if (compilerVersionResult.isSuccess()
        && !compilerVersionResult.stdoutLines.empty()
        && absl::StrContains(compilerVersionResult.stdoutLines[0], "NVIDIA")) {
      if (auto binDir = compilerPath.asRef().prefix()) {
        if (auto cudaDir = binDir->prefix()) {
          return std::make_unique<NvccToolchainInfo>(AbsolutePath(*cudaDir));
        }
      }
    }
    return nullptr;
  }
};

} // namespace

/*static*/ void ToolchainInfo::logDiagnosticsHint() {
  spdlog::info("compilation errors are suppressed by default, but can be "
               "turned on using --show-compiler-diagnostics");
}

/*static*/ void ToolchainInfo::logStdlibWarning() {
  spdlog::warn("may be unable to locate standard library headers");
  ToolchainInfo::logDiagnosticsHint();
}

/*static*/ std::unique_ptr<ToolchainInfo>
ToolchainInfo::infer(const AbsolutePath &compilerPath) {
  if (auto clangInfo = ClangToolchainInfo::tryInfer(compilerPath)) {
    return clangInfo;
  }

  if (auto gccInfo = GccToolchainInfo::tryInfer(compilerPath)) {
    return gccInfo;
  }

  if (auto nvccInfo = NvccToolchainInfo::tryInfer(compilerPath)) {
    return nvccInfo;
  }

  spdlog::warn("compiler at '{}' is not one of clang/clang++/gcc/g++/nvcc",
               compilerPath.asStringRef());
  ToolchainInfo::logStdlibWarning();
  return nullptr;
}

namespace scip_clang {
namespace compdb {

llvm::json::Value toJSON(const CommandObject &cmd) {
  // The keys here match what is present in a compilation database.
  // Skip the index as that's not expected by the rapidjson parser
  return llvm::json::Object{{"directory", cmd.workingDirectory},
                            {"file", cmd.filePath},
                            {"arguments", cmd.arguments}};
}

bool fromJSON(const llvm::json::Value &jsonValue, CommandObject &cmd,
              llvm::json::Path path) {
  // The keys match what is present in a compilation database.
  llvm::json::ObjectMapper mapper(jsonValue, path);
  return mapper && mapper.map("directory", cmd.workingDirectory)
         && mapper.map("file", cmd.filePath)
         && mapper.map("arguments", cmd.arguments);
}

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
        // NOTE(def: directory-field-is-absolute): While the JSON compilation
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
    this->wipCommand.workingDirectory = std::string(str, length);
    break;
  case Key::File:
    this->wipCommand.filePath = std::string(str, length);
    break;
  case Key::Command:
    this->wipCommand.arguments = scip_clang::unescapeCommandLine(
        clang::tooling::JSONCommandLineSyntax::AutoDetect,
        std::string_view(str, length));
    break;
  case Key::Arguments: // Validator makes sure we have an array outside.
    this->wipCommand.arguments.emplace_back(str, length);
    break;
  case Key::Output: // Do nothing
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

compdb::File compdb::File::open(const StdPath &path,
                                ValidationOptions validationOptions,
                                std::error_code &fileSizeError) {
  compdb::File compdbFile{};
  compdbFile.file = std::fopen(path.c_str(), "rb");
  if (!compdbFile.file) {
    return compdbFile;
  }
  auto size = std::filesystem::file_size(path, fileSizeError);
  if (fileSizeError) {
    return compdbFile;
  }
  if (validationOptions.tryDetectOutOfProjectRoot) {
    auto dirPath = path.lexically_normal();
    while (dirPath.has_parent_path() && dirPath.parent_path() != dirPath) {
      dirPath = dirPath.parent_path();
      auto maybeGitDirPath = dirPath / ".git";
      std::error_code error;
      auto status = std::filesystem::status(maybeGitDirPath, error);
      if (!error && status.type() == std::filesystem::file_type::directory) {
        auto cwd = std::filesystem::current_path();
        if (cwd != dirPath) {
          spdlog::warn(
              "found .git directory in {} but current working directory is {};"
              " did you invoke scip-clang from the project root?",
              dirPath.string(), cwd.string());
          spdlog::info(
              "invoking scip-clang from a directory other than the project root"
              " may lead to incorrect indexing results");
          break;
        }
      }
    }
  }
  compdbFile._sizeInBytes = size;
  compdbFile._commandCount = validateAndCountJobs(
      compdbFile._sizeInBytes, compdbFile.file, validationOptions);
  return compdbFile;
}

compdb::File
compdb::File::openAndExitOnErrors(const StdPath &path,
                                  ValidationOptions validationOptions) {
  std::error_code fileSizeError;
  auto compdbFile = compdb::File::open(path, validationOptions, fileSizeError);
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

// static
ParseOptions ParseOptions::create(size_t refillCount, bool forTesting) {
  ENFORCE(refillCount > 0);
  return ParseOptions{refillCount, /*adjustCommandLine*/ !forTesting,
                      /*skipNonMainFileTuEntries*/ !forTesting,
                      /*checkFilesExist*/ !forTesting};
}

void ResumableParser::initialize(compdb::File compdb, ParseOptions options) {
  auto averageJobSize = compdb.sizeInBytes() / compdb.commandCount();
  // Some customers have averageJobSize = 150KiB.
  // If numWorkers == 300 (very high core count machine),
  // then the computed hint will be ~88MiB. The 128MiB is rounded up from 88MiB.
  // The fudge factor of 2 is to allow for oversized jobs.
  auto bufferSize = std::min(size_t(128 * 1024 * 1024),
                             averageJobSize * 2 * options.refillCount);
  std::fseek(compdb.file, 0, SEEK_SET);
  this->handler = CommandObjectHandler(options.refillCount);
  this->jsonStreamBuffer.resize(bufferSize);
  this->compDbStream =
      rapidjson::FileReadStream(compdb.file, this->jsonStreamBuffer.data(),
                                this->jsonStreamBuffer.size());
  this->reader.IterativeParseInit();
  this->options = options;
  std::vector<std::string> extensions;
  // clang-format off
  // Via https://stackoverflow.com/a/3223792/2682729 (for C and C++)
  // For CUDA, see https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#basics-cdp1
  // and https://github.com/github-linguist/linguist/blob/master/lib/linguist/languages.yml#L1342-L1346
  // clang-format on
  for (auto ext : {"c", "C", "cc", "cpp", "CPP", "cxx", "c++", "cu"}) {
    extensions.emplace_back(llvm::Regex::escape(fmt::format(".{}", ext)));
  };
  this->fileExtensionRegex =
      llvm::Regex(fmt::format(".+({})$", fmt::join(extensions, "|")));
}

void ResumableParser::parseMore(std::vector<compdb::CommandObject> &out) {
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

  std::string pathBuffer;
  auto doesFileExist = [&](const std::string &path,
                           const std::string &base) -> bool {
    bool exists = false;
    bool isAbsolute = false;
    if (llvm::sys::path::is_absolute(path)) {
      isAbsolute = true;
      exists = llvm::sys::fs::exists(path);
    } else {
      pathBuffer.clear();
      fmt::format_to(std::back_inserter(pathBuffer), "{}{}{}", base,
                     std::filesystem::path::preferred_separator, path);
      exists = llvm::sys::fs::exists(pathBuffer);
    }
    if (!exists) {
      spdlog::warn(
          R"("file": "{}" in compilation database{} not found on disk)", path,
          isAbsolute ? "" : fmt::format(" (in directory '{}')", base));
    }
    return exists;
  };

  size_t initialSize = out.size();
  while (out.size() == initialSize) {
    while (!this->handler->reachedLimit()
           && !this->reader.IterativeParseComplete()) {
      this->reader.IterativeParseNext<rapidjson::kParseIterativeFlag>(
          this->compDbStream.value(), this->handler.value());
    }
    if (this->handler->commands.size() == 0) {
      break;
    }
    std::string pathBuffer;
    for (auto &cmd : this->handler->commands) {
      cmd.index = this->currentIndex;
      ++this->currentIndex;
      if (this->options.skipNonMainFileEntries) {
        if (!this->fileExtensionRegex.match(cmd.filePath)) {
          ++this->stats.skippedNonTuFileExtension;
          continue;
        }
      }
      if (this->options.checkFilesExist
          && !doesFileExist(cmd.filePath, cmd.workingDirectory)) {
        ++this->stats.skippedNonExistentTuFile;
        continue;
      }
      out.emplace_back(std::move(cmd));
    }
    this->handler->commands.clear();
  }

  if (this->options.adjustCommandLine) {
    for (auto &cmd : out) {
      if (cmd.arguments.empty()) {
        continue;
      }
      this->adjustCommandLine(cmd.workingDirectory, cmd.arguments);
    }
  }
}

void ResumableParser::adjustCommandLine(const std::string &directoryPath,
                                        std::vector<std::string> &commandLine) {
  auto &compilerOrWrapperPath = commandLine.front();
  auto it = this->toolchainInfoMap.find(compilerOrWrapperPath);
  if (it != this->toolchainInfoMap.end()) {
    auto &toolchain = it->second;
    if (toolchain) {
      toolchain->adjustCommandLine(commandLine);
    }
    return;
  }

  auto fail = [&]() {
    this->toolchainInfoMap.insert(
        {compilerOrWrapperPath, std::unique_ptr<ToolchainInfo>(nullptr)});
  };

  AbsolutePath compilerInvocationPath;
  if (compilerOrWrapperPath.find(std::filesystem::path::preferred_separator)
      == std::string::npos) {
    auto absPath = boost::process::search_path(compilerOrWrapperPath).native();
    if (absPath.empty()) {
      this->emitError(fmt::format(
          "scip-clang needs to be invoke '{0}' (found via the compilation"
          " database) to determine the resource directory, but couldn't find"
          " '{0}' on PATH. Hint: Use a modified PATH to invoke scip-clang,"
          " or change the compilation database to use absolute paths"
          " for the compiler.",
          compilerOrWrapperPath));
      return fail();
    }
    compilerInvocationPath =
        AbsolutePath(std::string(absPath.data(), absPath.size()));
  } else if (llvm::sys::path::is_relative(compilerOrWrapperPath)) {
    if (llvm::sys::path::is_absolute(directoryPath)) {
      compilerInvocationPath = AbsolutePath(
          scip_clang::joinPath(directoryPath, compilerOrWrapperPath));
    } else {
      spdlog::warn(
          R"("directory": "{}" key in compilation database is not an absolute path)"
          "; unable to determine resource directory for compiler: {}",
          directoryPath, compilerOrWrapperPath);
    }
  } else {
    ENFORCE(llvm::sys::path::is_absolute(compilerOrWrapperPath));
    compilerInvocationPath = AbsolutePath(std::string(compilerOrWrapperPath));
  }

  if (compilerInvocationPath.asStringRef().empty()) {
    return fail();
  }
  auto optToolchainInfo = ToolchainInfo::infer(compilerInvocationPath);
  if (!optToolchainInfo || !optToolchainInfo->isWellFormed()) {
    return fail();
  }

  auto [newIt, inserted] = this->toolchainInfoMap.emplace(
      compilerOrWrapperPath, std::move(optToolchainInfo));
  ENFORCE(inserted);
  newIt->second->adjustCommandLine(commandLine);
}

void ResumableParser::emitError(std::string &&error) {
  auto [it, inserted] = this->emittedErrors.emplace(std::move(error));
  if (inserted) {
    spdlog::error("{}", *it);
  }
}

} // namespace compdb
} // namespace scip_clang
