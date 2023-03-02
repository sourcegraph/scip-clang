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

enum class PartiallyDocumented {
  /// :smugcat:
  Documented,
  Undocumented,
};
