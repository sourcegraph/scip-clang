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