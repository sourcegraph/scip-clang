#include <cstdlib>
#include <string>

#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "llvm/Support/Regex.h"

#include "indexer/CliOptions.h"

namespace scip_clang {

IpcOptions CliOptions::ipcOptions() const {
  return IpcOptions{this->ipcSizeHintBytes, this->receiveTimeout,
                    this->driverId, this->workerId};
}

HeaderFilter::HeaderFilter(std::string &&re) {
  if (re.empty()) {
    this->matcher = {};
    return;
  }
  this->regexText = fmt::format("^({})$", re);
  this->matcher = {llvm::Regex(this->regexText)};
  std::string errMsg;
  if (!matcher->isValid(errMsg)) {
    spdlog::error("ill-formed regex {} for recording preprocessor history: {}",
                  this->regexText, errMsg);
    std::exit(EXIT_FAILURE);
  }
}

} // namespace scip_clang