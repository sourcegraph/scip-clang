#ifndef SCIP_CLANG_UTILS_H
#define SCIP_CLANG_UTILS_H

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
  const T &getValueNonConsuming() {
    ENFORCE(!this->movedOut, "use after move");
    ENFORCE(!this->consumed, "trying to access id for consumed guard");
    return this->value;
  }
};

/// Type to put inside other types to make them ConsumeOnce<>
/// to avoid having to deal with wrapping/unwrapping.
class Bomb final {
  ConsumeOnce<int> impl;

public:
  Bomb() : impl(0) {}
  Bomb(Bomb &&) = default;
  Bomb &operator=(Bomb &&) = default;

  void defuse() {
    (void)this->impl.getValueAndConsume();
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_TYPE_UTILS