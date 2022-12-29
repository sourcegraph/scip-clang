#ifndef SCIP_CLANG_CLI_OPTIONS_H
#define SCIP_CLANG_CLI_OPTIONS_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <sys/types.h>

#include "spdlog/fwd.h"

namespace scip_clang {

struct CliOptions {
  std::string compdbPath;
  std::string scipClangExecutablePath;

  std::chrono::seconds receiveTimeout;
  uint32_t numWorkers;

  spdlog::level::level_enum logLevel;

  bool isWorker;

  // An opaque ID provided by the driver for a worker to identify the
  // correct named memory map, guaranteed to be unique across
  // potentially multiple indexing jobs running in parallel at a
  // given instant.
  std::string driverId;
  // An opaque ID provided by the driver for a worker to identify
  // itself when sending results, guaranteed to be unique within an
  // indexing job at a given instant.
  uint64_t workerId;
};

} // namespace scip_clang

#endif // SCIP_CLANG_CLI_OPTIONS_H