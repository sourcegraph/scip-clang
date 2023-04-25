#ifndef SCIP_CLANG_ENFORCE_H
#define SCIP_CLANG_ENFORCE_H

// NOTE(ref: based-on-sorbet): Based on Sorbet files:
// - EnforceNoTimer.h
// - common/common.h

#include "spdlog/spdlog.h"

#include "indexer/Exception.h"
#include "indexer/Timer.h"
#include "indexer/Version.h"
#include "indexer/os/Os.h"

#define _MAYBE_ADD_COMMA(...) , ##__VA_ARGS__

// A faster version of ENFORCE that does not emit a timer. Useful for checks
// that happen extremely frequently and are O(1). Please avoid using unless
// ENFORCE shows up in profiles.
#define ENFORCE_NO_TIMER(x, ...)                                   \
  do {                                                             \
    if (::scip_clang::debugMode) {                                 \
      if (!(x)) {                                                  \
        /*::scip_clang::Exception::failInFuzzer(); */              \
        if (::scip_clang::stopInDebugger()) {                      \
          (void)!(x);                                              \
        }                                                          \
        ::scip_clang::Exception::enforceHandler(                   \
            #x, __FILE__, __LINE__ _MAYBE_ADD_COMMA(__VA_ARGS__)); \
      }                                                            \
    }                                                              \
  } while (false);

// Wraps input in double quotes. https://stackoverflow.com/a/6671729
#define Q(x) #x
#define QUOTED(x) Q(x)

// Used for cases like https://xkcd.com/2200/
// where there is some assumption that you believe should always hold.
// Please use this to explicitly write down what assumptions was the code
// written under. One day they might be violated and you'll help the next person
// debug the issue. Emits a timer so that expensive checks show up in traces in
// debug builds.
#define ENFORCE(...)                                                         \
  do {                                                                       \
    if (::scip_clang::debugMode) {                                           \
      auto __enforceTimer =                                                  \
          ::scip_clang::Timer(*(::spdlog::default_logger_raw()),             \
                              "ENFORCE(" __FILE__ ":" QUOTED(__LINE__) ")"); \
      ENFORCE_NO_TIMER(__VA_ARGS__);                                         \
    }                                                                        \
  } while (false);

#define ENFORCE_OR_WARN(_check, ...)          \
  do {                                        \
    ENFORCE(_check, __VA_ARGS__);             \
    if (!::scip_clang::debugMode && _check) { \
      spdlog::warn(__VA_ARGS__);              \
    }                                         \
  } while (false);

namespace scip_clang {

void initializeSymbolizer(const char *argv0, bool printStacktrace);

} // namespace scip_clang

#endif // SCIP_CLANG_ENFORCE_H