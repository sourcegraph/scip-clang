#ifndef SCIP_CLANG_LOGGING_H
#define SCIP_CLANG_LOGGING_H

#include <chrono>
#include <string>

namespace scip_clang {

// Simple RAII struct that logs the duration on destruction
struct LogTimerRAII {
  std::string message;
  std::chrono::time_point<std::chrono::steady_clock> start;

  LogTimerRAII(std::string &&message) : message(message) {
    this->start = std::chrono::steady_clock::now();
  }
  ~LogTimerRAII();
};

} // namespace scip_clang

#endif