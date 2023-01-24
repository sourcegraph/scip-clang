#include <array>
#include <climits>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

#include "spdlog/spdlog.h"

#include "indexer/os/Os.h"

namespace scip_clang {

std::string exec(std::string cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) {
    spdlog::error("popen() failed: {}", cmd.c_str());
    std::exit(EXIT_FAILURE);
  }
  while (feof(pipe.get()) == 0) {
    if (fgets(buffer.data(), 128, pipe.get()) != nullptr) {
      result += buffer.data();
    }
  }
  return result;
}

bool stopInDebugger() {
  if (amIBeingDebugged()) {
    __builtin_debugtrap();
    return true;
  }
  return false;
}

} // namespace scip_clang
