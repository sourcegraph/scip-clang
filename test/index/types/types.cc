// extra-args: -std=c++20
// format-options: showDocs

#include "types.h"

enum {
  ANON,
};

class C {
  class D {
  };
};

// Old MacDonald had a farm
// Ee i ee i o
enum D {
  // And on his farm he had some cows
  D1,
};

/// Ee i ee i oh
enum class EC {
  /// With a moo-moo here
  EC0,
  /// And a moo-moo there
  EC1 = EC0,
};

namespace a {
  class X {};

  namespace {
    class Y {};
  }
}

namespace has_anon_enum {
  enum {
    /* Here a moo, there a moo */
    F1,
    /** Everywhere a moo-moo */
    F2 = E2
  } f = F1;
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

#define VISIT(_name) Visit##_name

enum VISIT(Sightseeing) {
  VISIT(Museum),
};

// Regression test for https://github.com/sourcegraph/scip-clang/issues/105
enum class PartiallyDocumented {
  /// :smugcat:
  Documented,
  Undocumented,
};

template <typename T, int N>
class GenericClass {};

enum class E { E0 };

void f(GenericClass<E, int(E::E0)>) {
  (void)E::E0;
  (void)::E::E0;
#define QUALIFIED(enum_name, case_name) enum_name::case_name;
  (void)QUALIFIED(E, E0);
#undef QUALIFIED
}

/// Restating what's already implied by the name
class DocumentedForwardDeclaration;

class DocumentedForwardDeclaration { };

class Parent {};

class Child: Parent {};

template <class CRTPChild>
class CRTPBase {
  void castAndDoStuff() { static_cast<CRTPChild *>(this)->doStuff(); }
};

class CRTPChild: CRTPBase<CRTPChild> {
  void doStuff() { }
};

class DiamondBase {};
class Derived1 : public virtual DiamondBase {};
class Derived2 : public virtual DiamondBase {};
class Join : public Derived1, public Derived2 {};

struct L {};
auto trailing_return_type() -> L {
  // Explicit template param list on lambda needs C++20
  auto ignore_first = []<class T>(T, L l) -> L {
    return l;
  };
  return ignore_first("", L{});
}

struct M0 {
  using A = int;
};

struct M1: M0 {
  using B = M0;
  using B::A;
};