  // format-options: showDocs
  
  #include "types.h"
  
  enum {
//^^^^ definition [..] $anonymous_type_9a8b4e83cf46cb05_0#
    ANON,
//  ^^^^ definition [..] ANON.
  };
  
  class C {
//      ^ definition [..] C#
    class D {
//        ^ definition [..] C#D#
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
//        ^^^ reference [..] EC#EC0.
  };
  
  namespace a {
//          ^ definition [..] a/
    class X {};
//        ^ definition [..] a/X#
  
    namespace {
//  ^^^^^^^^^ definition [..] a/`$anonymous_namespace_types.cc`/
      class Y {};
//          ^ definition [..] a/`$anonymous_namespace_types.cc`/Y#
    }
  }
  
  namespace has_anon_enum {
//          ^^^^^^^^^^^^^ definition [..] has_anon_enum/
    enum {
//  ^^^^ definition [..] has_anon_enum/$anonymous_type_9a8b4e83cf46cb05_1#
      /* Here a moo, there a moo */
      F1,
//    ^^ definition [..] has_anon_enum/F1.
      /** Everywhere a moo-moo */
      F2 = E2
//    ^^ definition [..] has_anon_enum/F2.
//    documentation
//    | Everywhere a moo-moo 
//         ^^ reference [..] has_anon_enum/E2.
    } f = F1;
//        ^^ reference [..] has_anon_enum/F1.
  }
  
  class F0;
//      ^^ reference [..] F0#
  
  class F1 {
//      ^^ definition [..] F1#
    friend F0;
//         ^^ reference [..] F0#
  
    enum { ANON1 } anon1;
//  ^^^^ definition [..] F1#$anonymous_type_2#
//         ^^^^^ definition [..] F1#ANON1.
    enum { ANON2 = ANON1 } anon2;
//  ^^^^ definition [..] F1#$anonymous_type_3#
//         ^^^^^ definition [..] F1#ANON2.
//                 ^^^^^ reference [..] F1#ANON1.
  };
  
  class F0 {
//      ^^ definition [..] F0#
    friend class F1;
//               ^^ reference [..] F1#
  
    void f1(F1 *) { }
//          ^^ reference [..] F1#
  };
  
  void f() {
    class fC {
      void fCf() {
        class fCfC { };
      }
    };
  }
  
  #define VISIT(_name) Visit##_name
//        ^^^^^ definition [..] `types.cc:69:9`!
  
  enum VISIT(Sightseeing) {
//     ^^^^^ reference [..] `types.cc:69:9`!
//     ^^^^^ definition [..] VisitSightseeing#
    VISIT(Museum),
//  ^^^^^ definition [..] VisitMuseum.
//  ^^^^^ reference [..] `types.cc:69:9`!
  };
  
  // Regression test for https://github.com/sourcegraph/scip-clang/issues/105
  enum class PartiallyDocumented {
//           ^^^^^^^^^^^^^^^^^^^ definition [..] PartiallyDocumented#
    /// :smugcat:
    Documented,
//  ^^^^^^^^^^ definition [..] PartiallyDocumented#Documented.
//  documentation
//  | :smugcat:
    Undocumented,
//  ^^^^^^^^^^^^ definition [..] PartiallyDocumented#Undocumented.
  };
  
  template <typename T, int N>
  class GenericClass {};
//      ^^^^^^^^^^^^ definition [..] GenericClass#
  
  enum class E { E0 };
//           ^ definition [..] E#
//               ^^ definition [..] E#E0.
  
  void f(GenericClass<E, int(E::E0)>) {
//                    ^ reference [..] E#
//                           ^ reference [..] E#
//                              ^^ reference [..] E#E0.
    (void)E::E0;
//        ^ reference [..] E#
//           ^^ reference [..] E#E0.
    (void)::E::E0;
//          ^ reference [..] E#
//             ^^ reference [..] E#E0.
  #define QUALIFIED(enum_name, case_name) enum_name::case_name;
//        ^^^^^^^^^ definition [..] `types.cc:90:9`!
    (void)QUALIFIED(E, E0);
//        ^^^^^^^^^ reference [..] E#
//        ^^^^^^^^^ reference [..] E#E0.
//        ^^^^^^^^^ reference [..] `types.cc:90:9`!
  #undef QUALIFIED
//       ^^^^^^^^^ reference [..] `types.cc:90:9`!
  }
  
  /// Restating what's already implied by the name
  class DocumentedForwardDeclaration;
//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ reference [..] DocumentedForwardDeclaration#
  
  class DocumentedForwardDeclaration { };
//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DocumentedForwardDeclaration#
//      documentation
//      | Restating what's already implied by the name
  
  class Parent {};
//      ^^^^^^ definition [..] Parent#
  
  class Child: Parent {};
//      ^^^^^ definition [..] Child#
//      relation implementation=[..] Parent#
//             ^^^^^^ reference [..] Parent#
  
  template <class CRTPChild>
  class CRTPBase {
//      ^^^^^^^^ definition [..] CRTPBase#
    void castAndDoStuff() { static_cast<CRTPChild *>(this)->doStuff(); }
  };
  
  class CRTPChild: CRTPBase<CRTPChild> {
//      ^^^^^^^^^ definition [..] CRTPChild#
//      relation implementation=[..] CRTPBase#
//                          ^^^^^^^^^ reference [..] CRTPChild#
    void doStuff() { }
  };
