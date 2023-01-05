
#include <cstdlib>
#include <string>

#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "llvm/Support/Regex.h"

#include "indexer/CliOptions.h"

namespace scip_clang {

IpcOptions IpcOptions::testingStub = IpcOptions{.driverId = "testing"};

bool IpcOptions::isTestingStub() const {
  return this->driverId == "testing";
}

IpcOptions CliOptions::ipcOptions() const {
  return IpcOptions{this->receiveTimeout, this->driverId, this->workerId};
}

HeaderFilter::HeaderFilter(std::string &&re) {
  if (re.empty()) {
    this->matcher = {};
    return;
  }
  this->_regexText = fmt::format("^({})$", re);
  this->matcher = {llvm::Regex(this->_regexText)};
  std::string errMsg;
  if (!matcher->isValid(errMsg)) {
    spdlog::error("ill-formed regex {} for recording headers: {}",
                  this->_regexText, errMsg);
    std::exit(EXIT_FAILURE);
  }
}

} // namespace scip_clang