#ifndef SCIP_CLANG_EXCEPTION_H
#define SCIP_CLANG_EXCEPTION_H

// NOTE(ref: based-on-sorbet): Based on Sorbet files:
// - common/exception/Exception.h

#include <string_view>
#include <utility>

#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "indexer/os/Os.h"

namespace scip_clang {

class Exception final {
public:
  template <typename... TArgs>
  [[noreturn]] static bool raise(fmt::format_string<TArgs...> fmt,
                                 TArgs &&...args);

  [[noreturn]] static inline void notImplemented() {
    Exception::raise("Not Implemented");
  }

  static void printBacktrace() noexcept;
  // static void failInFuzzer() noexcept;

  [[noreturn]] static inline bool
  enforceHandler(std::string_view check, std::string_view file, int line) {
    Exception::enforceHandler(check, file, line, "(no message provided)");
  }
  template <typename... TArgs>
  [[noreturn]] static inline bool
  enforceHandler(std::string_view check, std::string_view file, int line,
                 fmt::format_string<TArgs...> message, TArgs &&...args) {
    Exception::raise("{}:{} enforced condition {} has failed: {}", file, line,
                     check, fmt::format(message, std::forward<TArgs>(args)...));
  }
};

extern std::string exceptionContext;

template <typename... TArgs>
[[noreturn]] bool Exception::raise(fmt::format_string<TArgs...> fmt,
                                   TArgs &&...args) {
  // Exception::failInFuzzer();
  std::string message = fmt::format(fmt, std::forward<TArgs>(args)...);

  if (!message.empty()) {
    spdlog::error("Exception::raise(): {}\n", message);
  } else {
    spdlog::error("Exception::raise() (no message)\n");
  }
  if (!exceptionContext.empty()) {
    spdlog::error("Context: {}", exceptionContext);
  }
  Exception::printBacktrace();
  scip_clang::stopInDebugger();
  std::exit(EXIT_FAILURE);
}

} // namespace scip_clang

#endif // SCIP_CLANG_EXCEPTION_H
