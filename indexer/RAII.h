#ifndef SCIP_CLANG_RAII_H
#define SCIP_CLANG_RAII_H

#include <utility>

#include "indexer/Enforce.h"

namespace scip_clang {

template <typename T> class ConsumeOnce final {
  T value;
  bool consumed;
  bool movedOut;

public:
  ConsumeOnce() = delete;
  ConsumeOnce(ConsumeOnce<T> &&old)
      : value(std::move(old.value)), consumed(old.consumed),
        movedOut(old.movedOut) {
    old.movedOut = true;
  }
  ConsumeOnce &operator=(ConsumeOnce &&old) {
    this->value = std::move(old.value);
    this->consumed = old.consumed;
    this->movedOut = old.movedOut;
    old.movedOut = true;
    return *this;
  }
  ConsumeOnce(const ConsumeOnce &) = delete;
  ConsumeOnce &operator=(const ConsumeOnce &) = delete;

  ConsumeOnce(T &&value)
      : value(std::move(value)), consumed(false), movedOut(false) {}

  ~ConsumeOnce() {
    if (!this->movedOut) {
      ENFORCE(this->consumed, "forgot to call getValueAndConsume");
    }
  }

  T getValueAndConsume() {
    ENFORCE(!this->movedOut, "use after move");
    ENFORCE(!this->consumed, "trying to consume worker guard twice");
    this->consumed = true;
    return std::move(this->value);
  }

  const T &getValueNonConsuming() const {
    ENFORCE(!this->movedOut, "use after move");
    ENFORCE(!this->consumed, "trying to access id for consumed guard");
    return this->value;
  }

  bool isUnconsumed() const {
    return !this->movedOut && !this->consumed;
  }
};

#ifdef DEBUG_MODE

/// Type to put inside other types to make them ConsumeOnce<>
/// to avoid having to deal with wrapping/unwrapping.
class Bomb final {
  ConsumeOnce<std::string> msg;

public:
  Bomb(std::string &&unconsumedHint) : msg(std::move(unconsumedHint)) {}
  Bomb(Bomb &&) = default;
  Bomb &operator=(Bomb &&) = default;

  ~Bomb() {
    if (this->msg.isUnconsumed()) {
      spdlog::error("unconsumed message: {}", this->msg.getValueNonConsuming());
    }
    this->msg.~ConsumeOnce<std::string>();
  }

  void defuse() {
    (void)this->msg.getValueAndConsume();
  }
};

#define BOMB_INIT(__msg) scip_clang::Bomb(__msg)

#else

struct Bomb {
  void defuse() {}
};

#define BOMB_INIT(__msg) scip_clang::Bomb()

#endif

} // namespace scip_clang

#endif // SCIP_CLANG_RAII_H
