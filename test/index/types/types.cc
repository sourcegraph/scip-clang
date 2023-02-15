#include "types.h"

enum {
  ANON,
};

class C {
  class D {
  };
};

enum D {
  D1,
};

enum class EC {
  EC0,
  EC1 = EC0,
};

namespace a {
  class X {};

  namespace {
    class Y {};
  }
}

namespace has_anon_enum {
  enum { F1, F2 = E2 } f = F1;
}

class F0;

class F1 {
  friend F0;

  enum { ANON1 } anon1;
  enum { ANON2 = ANON1 } anon2;
};

class F0 {
  friend class F1;

  void f1(F1 *) { }
};

void f() {
  class fC {
    void fCf() {
      class fCfC { };
    }
  };
}
