#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <string_view>

#include "spdlog/fmt/fmt.h"

#include "indexer/ProgressReporter.h"

namespace scip_clang {

ProgressReporter::ProgressReporter(bool active, std::string_view msg,
                                   size_t totalCount)
    : message(msg), totalCount(totalCount), countWidth(), active(active),
      isTty(false) {
  if (this->totalCount == 0) {
    countWidth = 1;
  } else {
    countWidth = std::log10(double(this->totalCount)) + 1;
  }
#if __linux__ || __APPLE__
  std::error_code ec;
  auto status = std::filesystem::status("/dev/stdout", ec);
  if (!ec) {
    this->isTty = (status.type() == std::filesystem::file_type::character);
  }
#endif
}

void ProgressReporter::report(size_t count, std::string_view extraData) const {
  if (!this->active) {
    return;
  }
  int maxExtraWidth = 256;
  auto backspaceCount = std::max(maxExtraWidth - int(extraData.size()), 0);
  if (this->isTty) {
    fmt::print("\r[{1:>{0}}/{2:>{0}}] {3} {4:<{5}}{6:\b<{7}}", countWidth,
               count, this->totalCount, this->message, extraData, maxExtraWidth,
               "", backspaceCount);
  } else {
    fmt::print("[{1:>{0}}/{2:>{0}}] {3} {4}\n", countWidth, count,
               this->totalCount, this->message, extraData);
  }
  std::flush(std::cout);
}

ProgressReporter::~ProgressReporter() {
  if (this->active && this->isTty) {
    fmt::print("\n");
  }
}

} // namespace scip_clang