// extra-args: -std=c++20

namespace a {
struct S {};
struct T {};
}

namespace b {
  using a::S, a::T;
}

namespace c {
  using S = a::S;
  using T = S;
}

namespace d {
  using S = a::S;
}

namespace e {
  using d::S; // equivalent to `using S = d::S;`
  using T = e::S;

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

namespace z {
  struct U {
    template <typename T>
    T identity(T t) { return t; }
  };

  struct V: U {
    int identity(int t, int) { return t; }
    using U::identity;
  };

  template <typename T>
  struct W {
    V v;

    T identity(T t) { return v.identity<T>(t); }
  };
}

namespace i {
  namespace j {
    void f() {}

    template <typename T>
    void ft(T) {}

    template <typename T>
    T zero = 0;
  }
  using j::f;
  void g() { f(); }

  using j::ft;
  void gt() { ft<int>(0); }

  namespace k {
    template <typename T>
    struct S {};

    template <typename T>
    using SAlias = S<T>;
  }

  using k::S;
  using SS = S<int>;

  using j::zero;
  static int zero_int = zero<int>;

  using k::SAlias;
  using SAliasInt = SAlias<int>;
}
