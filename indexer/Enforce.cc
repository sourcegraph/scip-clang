#include <atomic>
#include <string_view>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/str_replace.h"
#include "spdlog/spdlog.h"

#include "indexer/Enforce.h"
#include "indexer/Exception.h"

static std::atomic_bool printedContext;

static void logSignalError(const char *errorMessage) {
  if (!errorMessage) {
    return;
  }
  if (!printedContext) {
    spdlog::error("Context: {}", scip_clang::exceptionContext);
    printedContext = true;
  }
  auto message = std::string_view(errorMessage);
  if (message.ends_with('\n')) {
    message = message.substr(0, message.size() - 1);
  }
  spdlog::error("{}", absl::StrReplaceAll(
                          message, {{"@      ", "@"}, {"  (unknown)", ""}}));
}

namespace scip_clang {

void initializeSymbolizer(const char *argv0, bool printStacktrace) {
  absl::InitializeSymbolizer(argv0);
  if (printStacktrace) {
    auto options = absl::FailureSignalHandlerOptions{};
    options.writerfn = ::logSignalError;
    absl::InstallFailureSignalHandler(options);
  }
}

} // namespace scip_clang