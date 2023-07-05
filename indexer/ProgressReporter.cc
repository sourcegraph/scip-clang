#include <cmath>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string_view>

#include "spdlog/fmt/fmt.h"

#include "indexer/ProgressReporter.h"

namespace scip_clang {

ProgressReporter::ProgressReporter(bool active, std::string_view msg,
                                   size_t totalCount)
    : message(msg), totalCount(totalCount), countWidth(), active(active) {
  if (this->totalCount == 0) {
    countWidth = 1;
  } else {
    countWidth = std::log10(double(this->totalCount)) + 1;
  }
}

void ProgressReporter::report(size_t count, std::string_view extraData) const {
  if (!this->active) {
    return;
  }
  int maxExtraWidth = 256;
  auto backspaceCount = std::max(maxExtraWidth - int(extraData.size()), 0);
  fmt::print("\r[{1:>{0}}/{2:>{0}}] {3} {4:<{5}}{6:\b<{7}}", countWidth, count,
             this->totalCount, this->message, extraData, maxExtraWidth, "",
             backspaceCount);
  std::flush(std::cout);
}

ProgressReporter::~ProgressReporter() {
  if (this->active) {
    fmt::print("\n");
  }
}

} // namespace scip_clang