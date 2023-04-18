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
#include "indexer/Version.h"
#include "indexer/Worker.h"

static scip_clang::CliOptions parseArguments(int argc, char *argv[]) {
  scip_clang::CliOptions cliOptions{};
  cliOptions.scipClangExecutablePath = argv[0];
  using namespace std::chrono_literals;
  // cxxopts will print '(default: 10)' without context that it is based
  // on ncpus, so set the default here instead and print it separately.
  cliOptions.numWorkers = std::thread::hardware_concurrency();

  cxxopts::Options parser("scip-clang", "SCIP indexer for C-based languages");
  // clang-format off
  parser.add_options("")(
    "compdb-path",
    "Path to JSON compilation database",
    cxxopts::value<std::string>(cliOptions.compdbPath)->default_value("compile_commands.json"));
  parser.add_options("")(
    "index-output-path",
    "Path to write the SCIP index to",
    cxxopts::value<std::string>(cliOptions.indexOutputPath)->default_value("index.scip"));
  parser.add_options("")(
    "j,jobs",
    fmt::format(
      "Upper bound for number of indexing processes to run in parallel (default: NCPUs = {})",
      cliOptions.numWorkers),
    cxxopts::value<uint32_t>(cliOptions.numWorkers));
  parser.add_options("")(
    "log-level",
    "One of 'debug', 'info', 'warning' or 'error'",
    cxxopts::value<std::string>()->default_value("info"));
  parser.add_options("")(
    "temporary-output-dir",
    "Store temporary files under a specific directory instead of using system APIs."
    "If set, this directory will not be deleted after indexing is complete.",
    cxxopts::value<std::string>(cliOptions.temporaryOutputDir));
  parser.add_options("")(
    "show-compiler-diagnostics",
    "Show Clang diagnostics triggered when running semantic analysis."
    " Useful for debugging issues related to missing headers.",
    cxxopts::value<bool>(cliOptions.showCompilerDiagnostics));
  parser.add_options("")("version", "Show the version", cxxopts::value<bool>());
  parser.add_options("")("h,help", "Show help text", cxxopts::value<bool>());
  parser.add_options("Advanced")(
    "ipc-size-hint-bytes",
    "A size hint for how space is available for IPC (per worker)."
    " The actual space consumption may exceed this hint by ~15%.",
    cxxopts::value<size_t>(cliOptions.ipcSizeHintBytes)->default_value("2000000"));
    // ^ The default value of 2MB should be enough for typical invocations.
  parser.add_options("Advanced")(
    "print-statistics-path",
    "Print indexing related statistics in JSON format."
    " Caution: Timing information for individual TUs should not be compared"
    " directly across runs, as non-determinism may affect the number of files"
    " skipped by individual indexing jobs.",
    cxxopts::value<std::string>(cliOptions.statsFilePath));
  parser.add_options("Advanced")(
    "receive-timeout-seconds",
    "How long should the driver wait for a worker before marking it as timed out?",
    cxxopts::value<uint32_t>()->default_value("300"));
  parser.add_options("Advanced")(
    "deterministic",
    "Try to run everything in a deterministic fashion as much as possible."
    " Does not support deterministic work scheduling yet."
    " When using this flag, explicitly pass --temporary-output-dir to fix paths too.",
    cxxopts::value<bool>(cliOptions.deterministic));
  parser.add_options("Advanced")(
    "preprocessor-record-history-filter",
    "Regex for identifying files for which textual descriptions of preprocessor"
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
  parser.add_options("Advanced")(
    "measure-statistics",
    "[worker-only] Measure various statistics related to indexing",
    cxxopts::value<bool>(cliOptions.measureStatistics));
  parser.add_options("Internal")(
    "preprocessor-history-log-path",
    "[worker-only] Path to log preprocessor history, if applicable.",
    cxxopts::value<std::string>(cliOptions.preprocessorHistoryLogPath));
  parser.add_options("Internal")(
    "worker-mode",
    "[worker-only] Spawn an indexing worker instead of invoking the driver directly."
    " One of 'ipc', 'compdb' or 'testing'.",
    cxxopts::value<std::string>(cliOptions.workerMode)->default_value(""));
  parser.add_options("Internal")(
    "driver-id",
    "An opaque ID for the driver. Normally, this is only used by the driver "
    "to communicate with the worker, but it may be used to set the driver ID "
    "in tests as the PID is deterministic in a Bazel sandbox.",
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
  parser.add_options("Testing")(
    "testing",
    "Running for scip-clang internal tests.",
    cxxopts::value<bool>());

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
  if (result.count("version")) {
    fmt::print("{}", scip_clang::full_version_string);
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

  if (!cliOptions.workerMode.empty() && cliOptions.workerMode != "ipc"
      && cliOptions.workerMode != "compdb"
      && cliOptions.workerMode != "testing") {
    spdlog::error("--worker-mode must be 'ipc', 'compdb' or 'testing'");
    std::exit(EXIT_FAILURE);
  }

  cliOptions.receiveTimeout =
      std::chrono::seconds(result["receive-timeout-seconds"].as<uint32_t>());

  cliOptions.isTesting = result["testing"].count() > 0;

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
  bool isWorker = !cliOptions.workerMode.empty();
  auto loggerName =
      isWorker ? fmt::format("worker {}", cliOptions.workerId) : "driver";
  initializeGlobalLogger(loggerName, cliOptions.logLevel,
                         !cliOptions.workerFault.empty());
  if (isWorker) {
    return scip_clang::workerMain(std::move(cliOptions));
  }
  spdlog::debug("running {}", scip_clang::full_version_string);
  return scip_clang::driverMain(std::move(cliOptions));
}
