// extra-args: -std=c++20

namespace a {
struct S {};
}

namespace b {
  using a::S;
}

namespace c {
  using S = a::S;
  using T = S;
}

namespace d {
  using S = a::S;
}

namespace e {
  using d::S;

  template <typename X>
  struct R {};

  void f(R<S>) {}
}

typedef a::S aS;
typedef aS aS1;
using aS2 = aS;
using aS3 = aS1;
typedef aS2 aS4;

namespace f {
template <typename T>
struct A {
  using B = T;
  using C = B;
};
}

enum class LongLongEnum {
  X
};

namespace h {
  enum class EvenLongerEnum {
    Y
  };
}

void g() {
  // Since C++20
  using enum LongLongEnum;
  using enum h::EvenLongerEnum;
}
