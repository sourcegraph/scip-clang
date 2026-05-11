  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/types.cc`/
//documentation
//| File: types.cc
//kind File
  // format-options: showDocs
  
  #include "types.h"
//         ^^^^^^^^^ reference [..] `<file>/types.h`/
  
  enum {
//^^^^ definition [..] $anonymous_type_9a8b4e83cf46cb05_0#
//documentation
//| No documentation available.
//kind Enum
    ANON,
//  ^^^^ definition [..] ANON.
//  documentation
//  | No documentation available.
//  kind EnumMember
  };
  
  class C {
//      ^ definition [..] C#
//      documentation
//      | No documentation available.
//      kind Class
    class D {
//        ^ definition [..] C#D#
//        documentation
//        | No documentation available.
//        kind Class
    };
  };
  
  // Old MacDonald had a farm
  // Ee i ee i o
  enum D {
//     ^ definition [..] D#
//     documentation
//     | Old MacDonald had a farm
//     | Ee i ee i o
//     kind Enum
    // And on his farm he had some cows
    D1,
//  ^^ definition [..] D1.
//  documentation
//  | And on his farm he had some cows
//  kind EnumMember
  };
  
  /// Ee i ee i oh
  enum class EC {
//           ^^ definition [..] EC#
//           documentation
//           | Ee i ee i oh
//           kind Enum
    /// With a moo-moo here
    EC0,
//  ^^^ definition [..] EC#EC0.
//  documentation
//  | With a moo-moo here
//  kind EnumMember
    /// And a moo-moo there
    EC1 = EC0,
//  ^^^ definition [..] EC#EC1.
//  documentation
//  | And a moo-moo there
//  kind EnumMember
//        ^^^ reference [..] EC#EC0.
  };
  
  namespace a {
//          ^ definition [..] a/
//          documentation
//          | namespace a
//          kind Namespace
    class X {};
//        ^ definition [..] a/X#
//        documentation
//        | No documentation available.
//        kind Class
  
    namespace {
//  ^^^^^^^^^ definition [..] a/`$anonymous_namespace_types.cc`/
//  documentation
//  | anonymous namespace
//  kind Namespace
      class Y {};
//          ^ definition [..] a/`$anonymous_namespace_types.cc`/Y#
//          documentation
//          | No documentation available.
//          kind Class
    }
  }
  
  namespace has_anon_enum {
//          ^^^^^^^^^^^^^ definition [..] has_anon_enum/
//          documentation
//          | namespace has_anon_enum
//          kind Namespace
    enum {
//  ^^^^ definition [..] has_anon_enum/$anonymous_type_9a8b4e83cf46cb05_1#
//  documentation
//  | No documentation available.
//  kind Enum
      /* Here a moo, there a moo */
      F1,
//    ^^ definition [..] has_anon_enum/F1.
//    documentation
//    | Here a moo, there a moo
//    kind EnumMember
      /** Everywhere a moo-moo */
      F2 = E2
//    ^^ definition [..] has_anon_enum/F2.
//    documentation
//    | Everywhere a moo-moo
//    kind EnumMember
//         ^^ reference [..] has_anon_enum/E2.
    } f = F1;
//    ^ definition [..] has_anon_enum/f.
//    documentation
//    | No documentation available.
//    kind Variable
//        ^^ reference [..] has_anon_enum/F1.
  }
  
  class F0;
//      ^^ reference [..] F0#
  
  class F1 {
//      ^^ definition [..] F1#
//      documentation
//      | No documentation available.
//      kind Class
    friend F0;
//         ^^ reference [..] F0#
  
    enum { ANON1 } anon1;
//  ^^^^ definition [..] F1#$anonymous_type_2#
//  documentation
//  | No documentation available.
//  kind Enum
//         ^^^^^ definition [..] F1#ANON1.
//         documentation
//         | No documentation available.
//         kind EnumMember
//                 ^^^^^ definition [..] F1#anon1.
//                 documentation
//                 | No documentation available.
//                 kind Field
    enum { ANON2 = ANON1 } anon2;
//  ^^^^ definition [..] F1#$anonymous_type_3#
//  documentation
//  | No documentation available.
//  kind Enum
//         ^^^^^ definition [..] F1#ANON2.
//         documentation
//         | No documentation available.
//         kind EnumMember
//                 ^^^^^ reference [..] F1#ANON1.
//                         ^^^^^ definition [..] F1#anon2.
//                         documentation
//                         | No documentation available.
//                         kind Field
  };
  
  class F0 {
//      ^^ definition [..] F0#
//      documentation
//      | No documentation available.
//      kind Class
    friend class F1;
//               ^^ reference [..] F1#
  
    void f1(F1 *) { }
//       ^^ definition [..] F0#f1(9e7252de2ffc92f6).
//       documentation
//       | No documentation available.
//       kind Method
//          ^^ reference [..] F1#
  };
  
  void f() {
//     ^ definition [..] f(49f6e7a06ebc5aa8).
//     documentation
//     | No documentation available.
//     kind Function
    class fC {
//        ^^ definition [..] f(49f6e7a06ebc5aa8).fC#
//        documentation
//        | No documentation available.
//        kind Class
      void fCf() {
//         ^^^ definition [..] f(49f6e7a06ebc5aa8).fC#fCf(49f6e7a06ebc5aa8).
//         documentation
//         | No documentation available.
//         kind Method
        class fCfC { };
//            ^^^^ definition [..] f(49f6e7a06ebc5aa8).fC#fCf(49f6e7a06ebc5aa8).fCfC#
//            documentation
//            | No documentation available.
//            kind Class
      }
    };
  }
  
  #define VISIT(_name) Visit##_name
//        ^^^^^ definition [..] `types.cc:70:9`!
//        documentation
//        | No documentation available.
//        kind Macro
  
  enum VISIT(Sightseeing) {
//     ^^^^^ reference [..] `types.cc:70:9`!
//     ^^^^^ definition [..] VisitSightseeing#
//     documentation
//     | No documentation available.
//     kind Enum
    VISIT(Museum),
//  ^^^^^ definition [..] VisitMuseum.
//  documentation
//  | No documentation available.
//  kind EnumMember
//  ^^^^^ reference [..] `types.cc:70:9`!
  };
  
  // Regression test for https://github.com/sourcegraph/scip-clang/issues/105
  enum class PartiallyDocumented {
//           ^^^^^^^^^^^^^^^^^^^ definition [..] PartiallyDocumented#
//           documentation
//           | Regression test for https://github.com/sourcegraph/scip-clang/issues/105
//           kind Enum
    /// :smugcat:
    Documented,
//  ^^^^^^^^^^ definition [..] PartiallyDocumented#Documented.
//  documentation
//  | :smugcat:
//  kind EnumMember
    Undocumented,
//  ^^^^^^^^^^^^ definition [..] PartiallyDocumented#Undocumented.
//  documentation
//  | No documentation available.
//  kind EnumMember
  };
  
  template <typename T, int N>
//                   ^ definition local 0
//                   documentation
//                   | No documentation available.
//                   kind TypeParameter
//                          ^ definition local 1
//                          documentation
//                          | No documentation available.
//                          kind Parameter
  class GenericClass {};
//      ^^^^^^^^^^^^ definition [..] GenericClass#
//      documentation
//      | No documentation available.
//      kind Class
  
  enum class E { E0 };
//           ^ definition [..] E#
//           documentation
//           | No documentation available.
//           kind Enum
//               ^^ definition [..] E#E0.
//               documentation
//               | No documentation available.
//               kind EnumMember
  
  void f(GenericClass<E, int(E::E0)>) {
//     ^ definition [..] f(a9a88f5fb6852c6b).
//     documentation
//     | No documentation available.
//     kind Function
//       ^^^^^^^^^^^^ reference [..] GenericClass#
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
//        ^^^^^^^^^ definition [..] `types.cc:91:9`!
//        documentation
//        | No documentation available.
//        kind Macro
    (void)QUALIFIED(E, E0);
//        ^^^^^^^^^ reference [..] E#
//        ^^^^^^^^^ reference [..] E#E0.
//        ^^^^^^^^^ reference [..] `types.cc:91:9`!
  #undef QUALIFIED
//       ^^^^^^^^^ reference [..] `types.cc:91:9`!
  }
  
  /// Restating what's already implied by the name
  class DocumentedForwardDeclaration;
//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ reference [..] DocumentedForwardDeclaration#
  
  class DocumentedForwardDeclaration { };
//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DocumentedForwardDeclaration#
//      documentation
//      | Restating what's already implied by the name
//      kind Class
  
  class Parent {};
//      ^^^^^^ definition [..] Parent#
//      documentation
//      | No documentation available.
//      kind Class
  
  class Child: Parent {};
//      ^^^^^ definition [..] Child#
//      documentation
//      | No documentation available.
//      kind Class
//      relation implementation [..] Parent#
//             ^^^^^^ reference [..] Parent#
  
  template <class CRTPChild>
//                ^^^^^^^^^ definition local 2
//                documentation
//                | No documentation available.
//                kind TypeParameter
  class CRTPBase {
//      ^^^^^^^^ definition [..] CRTPBase#
//      documentation
//      | No documentation available.
//      kind Class
    void castAndDoStuff() { static_cast<CRTPChild *>(this)->doStuff(); }
//       ^^^^^^^^^^^^^^ definition [..] CRTPBase#castAndDoStuff(49f6e7a06ebc5aa8).
//       documentation
//       | No documentation available.
//       kind Method
//                                      ^^^^^^^^^ reference local 2
  };
  
  class CRTPChild: CRTPBase<CRTPChild> {
//      ^^^^^^^^^ definition [..] CRTPChild#
//      documentation
//      | No documentation available.
//      kind Class
//      relation implementation [..] CRTPBase#
//                 ^^^^^^^^ reference [..] CRTPBase#
//                          ^^^^^^^^^ reference [..] CRTPChild#
    void doStuff() { }
//       ^^^^^^^ definition [..] CRTPChild#doStuff(49f6e7a06ebc5aa8).
//       documentation
//       | No documentation available.
//       kind Method
  };
  
  class DiamondBase {};
//      ^^^^^^^^^^^ definition [..] DiamondBase#
//      documentation
//      | No documentation available.
//      kind Class
  class Derived1 : public virtual DiamondBase {};
//      ^^^^^^^^ definition [..] Derived1#
//      documentation
//      | No documentation available.
//      kind Class
//      relation implementation [..] DiamondBase#
//                                ^^^^^^^^^^^ reference [..] DiamondBase#
  class Derived2 : public virtual DiamondBase {};
//      ^^^^^^^^ definition [..] Derived2#
//      documentation
//      | No documentation available.
//      kind Class
//      relation implementation [..] DiamondBase#
//                                ^^^^^^^^^^^ reference [..] DiamondBase#
  class Join : public Derived1, public Derived2 {};
//      ^^^^ definition [..] Join#
//      documentation
//      | No documentation available.
//      kind Class
//      relation implementation [..] Derived1#
//      relation implementation [..] Derived2#
//      relation implementation [..] DiamondBase#
//                    ^^^^^^^^ reference [..] Derived1#
//                                     ^^^^^^^^ reference [..] Derived2#
  
  struct L {};
//       ^ definition [..] L#
//       documentation
//       | No documentation available.
//       kind Struct
  auto trailing_return_type() -> L {
//     ^^^^^^^^^^^^^^^^^^^^ definition [..] trailing_return_type(693bfa61ed1914d5).
//     documentation
//     | No documentation available.
//     kind Function
//                               ^ reference [..] L#
    // Explicit template param list on lambda needs C++20
    auto ignore_first = []<class T>(T, L l) -> L {
//       ^^^^^^^^^^^^ definition local 3
//       documentation
//       | No documentation available.
//       kind Variable
//                               ^ definition local 4
//                               documentation
//                               | No documentation available.
//                               kind TypeParameter
//                                  ^ reference local 4
//                                     ^ reference [..] L#
//                                       ^ definition local 5
//                                       documentation
//                                       | No documentation available.
//                                       kind Parameter
//                                             ^ reference [..] L#
      return l;
//           ^ reference local 5
    };
    return ignore_first("", L{});
//         ^^^^^^^^^^^^ reference local 3
//                     ^ reference [..] trailing_return_type(693bfa61ed1914d5).$anonymous_type_4#`operator()`(b691caff6c7f530).
//                          ^ reference [..] L#
  }
  
  struct M0 {
//       ^^ definition [..] M0#
//       documentation
//       | No documentation available.
//       kind Struct
    using A = int;
//        ^ definition [..] M0#A#
//        documentation
//        | No documentation available.
//        kind TypeAlias
  };
  
  struct M1: M0 {
//       ^^ definition [..] M1#
//       documentation
//       | No documentation available.
//       kind Struct
//       relation implementation [..] M0#
//           ^^ reference [..] M0#
    using B = M0;
//        ^ definition [..] M1#B#
//        documentation
//        | No documentation available.
//        kind TypeAlias
//            ^^ reference [..] M0#
    using B::A;
//        ^ reference [..] M1#B#
//           ^ reference [..] M0#A#
//           ^ definition [..] M1#A#
//           documentation
//           | No documentation available.
  };
