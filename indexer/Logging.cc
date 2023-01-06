#include <chrono>

#include "spdlog/spdlog.h"

#include "indexer/Logging.h"

namespace scip_clang {

LogTimerRAII::~LogTimerRAII() {
  auto end = std::chrono::steady_clock::now();
  auto diff = end - this->start;
  using namespace std::chrono_literals;
  auto diffSec =
      double(
          std::chrono::duration_cast<std::chrono::milliseconds>(diff).count())
      / 1000.0;
  spdlog::debug("timing for {}: {:.1f}s", this->message, diffSec);
}

} // namespace scip_clang
