#ifndef SCIP_CLANG_TIMER_H
#define SCIP_CLANG_TIMER_H

// NOTE(ref: based-on-sorbet): Based on Sorbet files:
// - common/Counters.h
// - common/Timer.h

#include "spdlog/logger.h"

namespace scip_clang {

struct ConstExprStr {
  char const *str;
  std::size_t size;

  template <std::size_t N>
  constexpr ConstExprStr(char const (&s)[N]) : str(s), size(N - 1) {}

  ConstExprStr() = delete;
};

class Microseconds {
  int64_t value;

public:
  Microseconds(int64_t value) : value(value) {}
  Microseconds operator-(Microseconds other) const {
    return Microseconds{this->value - other.value};
  }
  bool operator>(Microseconds other) const {
    return this->value > other.value;
  }
  int64_t usec() const {
    return this->value;
  }
};

class Timer {
  spdlog::logger &logger;
  ConstExprStr name;
  Microseconds start;
  bool canceled;

public:
  Timer(spdlog::logger &logger, ConstExprStr name);
  Timer(const Timer &) = delete;
  Timer(Timer &&other)
      : logger(other.logger), name(other.name), start(other.start),
        canceled(other.canceled) {
    other.canceled = true;
  };
  ~Timer();
};

} // namespace scip_clang

#endif // SCIP_CLANG_TIMER_H