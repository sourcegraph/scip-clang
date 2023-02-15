  #include "types.h"
  
  enum {
//^^^^ definition [..] $anontype_9a8b4e83cf46cb05_0#
    ANON,
//  ^^^^ definition [..] ANON.
  };
  
  class C {
    class D {
    };
  };
  
  enum D {
//     ^ definition [..] D#
    D1,
//  ^^ definition [..] D1.
  };
  
  enum class EC {
//           ^^ definition [..] EC#
    EC0,
//  ^^^ definition [..] EC#EC0.
    EC1 = EC0,
//  ^^^ definition [..] EC#EC1.
  };
  
  namespace a {
//          ^ definition [..] a/
    class X {};
  
    namespace {
//  ^^^^^^^^^ definition [..] $ANON/types.cc/
      class Y {};
    }
  }
  
  namespace has_anon_enum {
//          ^^^^^^^^^^^^^ definition [..] has_anon_enum/
    enum { F1, F2 = E2 } f = F1;
//  ^^^^ definition [..] has_anon_enum/$anontype_9a8b4e83cf46cb05_1#
//         ^^ definition [..] has_anon_enum/F1.
//             ^^ definition [..] has_anon_enum/F2.
  }
  
  class F0;
  
  class F1 {
    friend F0;
  
    enum { ANON1 } anon1;
//  ^^^^ definition [..] F1#$anontype_2#
//         ^^^^^ definition [..] F1#ANON1.
    enum { ANON2 = ANON1 } anon2;
//  ^^^^ definition [..] F1#$anontype_3#
//         ^^^^^ definition [..] F1#ANON2.
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
