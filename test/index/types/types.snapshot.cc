  // format-options: showDocs
  
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
  
  // Old MacDonald had a farm
  // Ee i ee i o
  enum D {
//     ^ definition [..] D#
    // And on his farm he had some cows
    D1,
//  ^^ definition [..] D1.
  };
  
  /// Ee i ee i oh
  enum class EC {
//           ^^ definition [..] EC#
//           documentation
//           | Ee i ee i oh
    /// With a moo-moo here
    EC0,
//  ^^^ definition [..] EC#EC0.
//  documentation
//  | With a moo-moo here
    /// And a moo-moo there
    EC1 = EC0,
//  ^^^ definition [..] EC#EC1.
//  documentation
//  | And a moo-moo there
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
    enum {
//  ^^^^ definition [..] has_anon_enum/$anontype_9a8b4e83cf46cb05_1#
      /* Here a moo, there a moo */
      F1,
//    ^^ definition [..] has_anon_enum/F1.
      /** Everywhere a moo-moo */
      F2 = E2
//    ^^ definition [..] has_anon_enum/F2.
//    documentation
//    | Everywhere a moo-moo 
    } f = F1;
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
  
  #define VISIT(_name) Visit##_name
//        ^^^^^ definition [..] types.cc:69:9#
  
  enum VISIT(Sightseeing) {
//     ^^^^^ reference [..] types.cc:69:9#
//     ^^^^^ definition [..] VisitSightseeing#
    VISIT(Museum),
//  ^^^^^ definition [..] VisitMuseum.
//  ^^^^^ reference [..] types.cc:69:9#
  };
  
  enum class PartiallyDocumented {
//           ^^^^^^^^^^^^^^^^^^^ definition [..] PartiallyDocumented#
    /// :smugcat:
    Documented,
//  ^^^^^^^^^^ definition [..] PartiallyDocumented#Documented.
//  documentation
//  | :smugcat:
    Undocumented,
//  ^^^^^^^^^^^^ definition [..] PartiallyDocumented#Undocumented.
//  documentation
//  | :smugcat:
  };
