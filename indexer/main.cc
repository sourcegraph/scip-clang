#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <thread>

#include "cxxopts.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/fmt/ranges.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include "indexer/CliOptions.h"
#include "indexer/Driver.h"
#include "indexer/Enforce.h"
#include "indexer/Worker.h"

static scip_clang::CliOptions parseArguments(int argc, char *argv[]) {
  scip_clang::CliOptions cliOptions{};
  cliOptions.scipClangExecutablePath = argv[0];
  using namespace std::chrono_literals;
  auto defaultReceiveTimeoutSeconds = "120";
  // cxxopts will print '(default: 10)' without context that it is based
  // on ncpus, so set the default here instead and print it separately.
  cliOptions.numWorkers = std::thread::hardware_concurrency();
  auto defaultLogLevel = "info";
  auto defaultCompdbPath = "compile_commands.json";

  cxxopts::Options parser("scip-clang", "SCIP indexer for C-based languages");
  // clang-format off
  parser.add_options("")(
    "compdb-path",
    fmt::format("Path to JSON compilation database", defaultCompdbPath),
    cxxopts::value<std::string>(cliOptions.compdbPath)->default_value(defaultCompdbPath));
  parser.add_options("")(
    "j,jobs",
    fmt::format("How many indexing processes to run in parallel? (default: NCPUs = {})", cliOptions.numWorkers),
    cxxopts::value<uint32_t>(cliOptions.numWorkers));
  parser.add_options("")(
    "log-level",
    fmt::format("One of 'debug', 'info', 'warning' or 'error'", defaultLogLevel),
    cxxopts::value<std::string>()->default_value(defaultLogLevel));
  parser.add_options("")(
    "temporary-output-dir",
    "Store temporary files under a specific directory instead of using system APIs."
    "If set, this directory will not be deleted after indexing is complete.",
    cxxopts::value<std::string>(cliOptions.temporaryOutputDir));
  parser.add_options("")("h,help", "Show help text", cxxopts::value<bool>());
  // TODO(def: add-version): Add a --version flag
  parser.add_options("Advanced")(
    "receive-timeout-seconds",
    fmt::format("How long the driver should wait for a worker before marking "
                "it as timed out?", defaultReceiveTimeoutSeconds),
    cxxopts::value<uint32_t>()->default_value(defaultReceiveTimeoutSeconds));
  parser.add_options("Advanced")(
    "deterministic",
    "Try to run everything in a deterministic fashion as much as possible."
    " Does not support deterministic work scheduling yet."
    " When using this flag, explicitly pass --temporary-output-dir to fix paths too.",
    cxxopts::value<bool>(cliOptions.deterministic));
  parser.add_options("Advanced")(
    "preprocessor-record-history-filter",
    "Regex for identifying headers for which textual descriptions of preprocessor"
    " effects should be recorded while computing transcripts, instead of"
    " only maintaining a running hash value. The effects are recorded in YAML"
    " format under --supplementary-output-dir.",
    cxxopts::value<std::string>(cliOptions.preprocessorRecordHistoryFilterRegex));
  parser.add_options("Advanced")(
    "supplementary-output-dir",
    "Path to directory for recording supplementary outputs, such as various log files.",
    cxxopts::value<std::string>(cliOptions.supplementaryOutputDir)->default_value("scip-clang-supplementary-output"));
  parser.add_options("Advanced")(
    "help-all",
    "Show all command-line flags, including internal ones and ones for testing.",
    cxxopts::value<bool>());
  parser.add_options("Internal")(
    "preprocessor-history-log-path",
    "[worker-only] Path to log preprocessor history, if applicable.",
    cxxopts::value<std::string>(cliOptions.preprocessorHistoryLogPath));
  parser.add_options("Internal")(
    "worker",
    "[worker-only] Spawn an indexing worker instead of invoking the driver directly",
    cxxopts::value<bool>(cliOptions.isWorker));
  parser.add_options("Internal")(
    "driver-id",
    "[worker-only] An opaque ID for the driver.",
    cxxopts::value<std::string>(cliOptions.driverId));
  parser.add_options("Internal")(
    "worker-id",
    "[worker-only] An opaque ID for the worker itself.",
    cxxopts::value<uint64_t>(cliOptions.workerId));
  parser.add_options("Testing")(
    "force-worker-fault",
    "One of 'crash', 'sleep' or 'spin'."
    " Forces faulty behavior in a worker process instead of normal processing.",
    cxxopts::value<std::string>(cliOptions.workerFault)->default_value(""));

  // TODO(def: flag-passthrough, issue: https://github.com/sourcegraph/scip-clang/issues/23)
  // Support passing through CLI flags to Clang, similar to --extra-arg in lsif-clang

  // clang-format on

  parser.allow_unrecognised_options();

  cxxopts::ParseResult result = parser.parse(argc, argv);

  if (result.count("help") || result.count("h")) {
    fmt::print("{}\n", parser.help({"", "Advanced"}));
    std::exit(EXIT_SUCCESS);
  }
  if (result.count("help-all")) {
    fmt::print("{}\n", parser.help());
    std::exit(EXIT_SUCCESS);
  }

  if (!result.unmatched().empty()) {
    fmt::print(stderr, "error: unknown argument(s) {}\n", result.unmatched());
    fmt::print(stderr, "{}\n", parser.help());
    std::exit(EXIT_FAILURE);
  }

  auto level = result["log-level"].as<std::string>();
  if (level == "debug") {
    cliOptions.logLevel = spdlog::level::level_enum::debug;
  } else if (level == "info") {
    cliOptions.logLevel = spdlog::level::level_enum::info;
  } else if (level == "warning") {
    cliOptions.logLevel = spdlog::level::level_enum::warn;
  } else if (level == "error") {
    cliOptions.logLevel = spdlog::level::level_enum::err;
  } else {
    spdlog::warn("unknown argument '{}' for --log-level; see scip-clang "
                 "--help for recognized levels",
                 level);
  }

  cliOptions.receiveTimeout =
      std::chrono::seconds(result["receive-timeout-seconds"].as<uint32_t>());

  for (int i = 0; i < argc; ++i) {
    cliOptions.originalArgv.push_back(argv[i]);
  }

  return cliOptions;
}

static void initializeGlobalLogger(std::string name,
                                   spdlog::level::level_enum level,
                                   bool forTesting) {
  auto defaultLogger = spdlog::stderr_color_mt(name);
  defaultLogger->set_level(level);
  if (forTesting) {
    defaultLogger->set_pattern("[%l] %n: %v");
  } else {
    defaultLogger->set_pattern("[%T %^%l%$] %-10n: %v");
  }
  spdlog::set_default_logger(defaultLogger);
}

int main(int argc, char *argv[]) {
  scip_clang::initializeSymbolizer(argv[0]);
  auto cliOptions = parseArguments(argc, argv);
  auto loggerName = cliOptions.isWorker
                        ? fmt::format("worker {}", cliOptions.workerId)
                        : "driver";
  initializeGlobalLogger(loggerName, cliOptions.logLevel,
                         !cliOptions.workerFault.empty());
  if (cliOptions.isWorker) {
    return scip_clang::workerMain(std::move(cliOptions));
  }
  return scip_clang::driverMain(std::move(cliOptions));
}
