#include <chrono>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <thread>

#include "cxxopts.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include "indexer/CliOptions.h"
#include "indexer/Driver.h"
#include "indexer/Worker.h"

static scip_clang::CliOptions parseArguments(int argc, char *argv[]) {
  scip_clang::CliOptions cliOptions;
  cliOptions.scipClangExecutablePath = argv[0];
  using namespace std::chrono_literals;
  cliOptions.receiveTimeout = 120s;
  cliOptions.numWorkers = std::thread::hardware_concurrency();
  cliOptions.logLevel = spdlog::level::level_enum::info;

  cxxopts::Options options("scip-clang", "SCIP indexer for C-based languages");
  options.add_options("")("j,jobs",
                          fmt::format("How many indexing processes to run in "
                                      "parallel? (default: NCPUs = {})",
                                      cliOptions.numWorkers),
                          cxxopts::value<uint32_t>(cliOptions.numWorkers));
  options.add_options("")(
      "log-level", "One of 'debug', 'info' (default), 'warning' or 'error'",
      cxxopts::value<std::string>());
  options.add_options("advanced")(
      "receive-timeout-seconds",
      fmt::format("How long the driver should wait for a worker before marking "
                  "it as timed out? (default: {}s)",
                  std::chrono::duration_cast<std::chrono::seconds>(
                      cliOptions.receiveTimeout)
                      .count()),
      cxxopts::value<uint32_t>());
  options.add_options("internal")("worker",
                                  "[worker-only] Spawn an indexing worker "
                                  "instead of invoking the driver directly",
                                  cxxopts::value<bool>(cliOptions.isWorker));
  options.add_options("internal")(
      "driver-pid", "[worker-only] An opaque ID for the driver.",
      cxxopts::value<std::string>(cliOptions.driverId));
  options.add_options("internal")(
      "worker-pid", "[worker-only] An opaque ID for the worker itself.",
      cxxopts::value<uint64_t>(cliOptions.workerId));
  cxxopts::ParseResult result = options.parse(argc, argv);

  if (result.count("log-level") > 0) {
    auto level = result["log-level"].as<std::string>();
    if (level == "debug") {
      cliOptions.logLevel = spdlog::level::level_enum::debug;
    } else if (level == "warning") {
      cliOptions.logLevel = spdlog::level::level_enum::warn;
    } else if (level == "error") {
      cliOptions.logLevel = spdlog::level::level_enum::err;
    } else if (level != "info") {
      spdlog::warn("unknown argument '{}' for --log-level; use 'debug', "
                   "'info', 'warning' or 'error'",
                   level);
    }
  }
  if (result.count("receive-timeout-seconds") > 0) {
    cliOptions.receiveTimeout =
        std::chrono::seconds(result["receive-timeout-seconds"].as<uint32_t>());
  }

  return cliOptions;
}

static void initializeGlobalLogger(std::string name,
                                   spdlog::level::level_enum level) {
  auto defaultLogger = spdlog::stderr_color_mt(name);
  defaultLogger->set_level(level);
  defaultLogger->set_pattern("[%T %^%l%$] %-10n: %v");
  spdlog::set_default_logger(defaultLogger);
}

int main(int argc, char *argv[]) {
  auto cliOptions = parseArguments(argc, argv);
  auto loggerName = cliOptions.isWorker
                        ? fmt::format("worker {}", cliOptions.workerId)
                        : "driver";
  initializeGlobalLogger(loggerName, cliOptions.logLevel);
  if (cliOptions.isWorker) {
    return scip_clang::workerMain(std::move(cliOptions));
  }
  return scip_clang::driverMain(std::move(cliOptions));
}
