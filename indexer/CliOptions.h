#ifndef SCIP_CLANG_CLI_OPTIONS_H
#define SCIP_CLANG_CLI_OPTIONS_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>
#include <vector>

#include "spdlog/fwd.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"

namespace scip_clang {

struct IpcOptions {
  size_t ipcSizeHintBytes;
  std::chrono::seconds receiveTimeout;
  std::string driverId;
  uint64_t workerId;
};

struct CliOptions {
  std::string compdbPath;
  std::string scipClangExecutablePath;
  std::string temporaryOutputDir;
  std::string indexOutputPath;
  std::string statsFilePath;
  std::string packageMapPath;
  bool showCompilerDiagnostics;
  bool showProgress;

  size_t ipcSizeHintBytes;
  std::chrono::seconds receiveTimeout;
  uint32_t numWorkers;

  spdlog::level::level_enum logLevel;

  bool deterministic;
  std::string preprocessorRecordHistoryFilterRegex;
  std::string supplementaryOutputDir;

  // For recording inside the index.
  std::vector<std::string> originalArgv;

  // For testing only
  bool isTesting;
  std::string workerFault;
  bool noStacktrace;

  // Worker-specific options

  std::string workerMode;

  bool measureStatistics;

  std::string preprocessorHistoryLogPath;

  // An opaque ID provided by the driver for a worker to identify the
  // correct named memory map, guaranteed to be unique across
  // potentially multiple indexing jobs running in parallel at a
  // given instant.
  std::string driverId;
  // An opaque ID provided by the driver for a worker to identify
  // itself when sending results, guaranteed to be unique within an
  // indexing job at a given instant.
  uint64_t workerId;

  IpcOptions ipcOptions() const;
};

class HeaderFilter final {
  /// The original text of the regex, because \c llvm::Regex doesn't expose
  /// an API for serializing to a string.
  std::string regexText;
  std::optional<llvm::Regex> matcher;

public:
  HeaderFilter() = default;
  HeaderFilter(HeaderFilter &&) = default;
  HeaderFilter &operator=(HeaderFilter &&) = default;

  /// Try to initialize a HeaderFilter from \p regexText.
  /// Logs an error and exits the program if \p regexText is ill-formed.
  HeaderFilter(std::string &&regexText);

  bool matches(std::string_view data) const {
    if (matcher && matcher->match(llvm::StringRef(data))) {
      return true;
    }
    return false;
  }

  bool isIdentity() const {
    return this->regexText.empty();
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_CLI_OPTIONS_H