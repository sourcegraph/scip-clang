#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include "indexer/Logging.h"

namespace scip_clang {

void initialize_global_logger(std::string name) {
  auto default_logger = spdlog::stderr_color_mt(name);
  spdlog::set_default_logger(default_logger);
  spdlog::set_pattern("[%T %^%l%$] %-10n: %v");
}

} // namespace scip_clang