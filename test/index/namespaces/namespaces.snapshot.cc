  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/namespaces.cc`/
//kind File
  
  #include "system_header.h"
//         ^^^^^^^^^^^^^^^^^ reference [..] `<file>/system_header.h`/
  
  namespace a {
//          ^ definition [..] a/
//          kind Namespace
  }
  
  // nested namespace definition allowed since C++17
  namespace a::b {
//          ^ definition [..] a/
//          kind Namespace
//             ^ definition [..] a/b/
//             kind Namespace
  }
  
  namespace {
//^^^^^^^^^ definition [..] `$anonymous_namespace_namespaces.cc`/
//kind Namespace
  }
  
  inline namespace xx {
//                 ^^ definition [..] xx/
//                 kind Namespace
  }
  
  namespace z {
//          ^ definition [..] z/
//          kind Namespace
  
  inline namespace {
//       ^^^^^^^^^ definition [..] z/`$anonymous_namespace_namespaces.cc`/
//       kind Namespace
  }
  
  }
  
  // inline nested namespace definition allowed since C++20
  namespace z::inline y {
//          ^ definition [..] z/
//          kind Namespace
//                    ^ definition [..] z/y/
//                    kind Namespace
  }
  
  namespace c {
//          ^ definition [..] c/
//          kind Namespace
  class C {
//      ^ definition [..] c/C#
//      kind Class
  };
  }
  
  using C = c::C;
//      ^ definition [..] C#
//      kind TypeAlias
//          ^ reference [..] c/
//             ^ reference [..] c/C#
  
  #define EXPAND_TO_NAMESPACE \
//        ^^^^^^^^^^^^^^^^^^^ definition [..] `namespaces.cc:36:9`!
//        kind Macro
    namespace from_macro {}
  
  EXPAND_TO_NAMESPACE
//^^^^^^^^^^^^^^^^^^^ definition [..] from_macro/
//kind Namespace
//^^^^^^^^^^^^^^^^^^^ reference [..] `namespaces.cc:36:9`!
  
  #define EXPAND_TO_NAMESPACE_2 EXPAND_TO_NAMESPACE
//        ^^^^^^^^^^^^^^^^^^^^^ definition [..] `namespaces.cc:41:9`!
//        kind Macro
//                              ^^^^^^^^^^^^^^^^^^^ reference [..] `namespaces.cc:36:9`!
  
  EXPAND_TO_NAMESPACE_2
//^^^^^^^^^^^^^^^^^^^^^ definition [..] from_macro/
//kind Namespace
//^^^^^^^^^^^^^^^^^^^^^ reference [..] `namespaces.cc:41:9`!
  
  #define IDENTITY(x) x
//        ^^^^^^^^ definition [..] `namespaces.cc:45:9`!
//        kind Macro
  
  IDENTITY(namespace in_macro { })
//^^^^^^^^ definition [..] in_macro/
//kind Namespace
//^^^^^^^^ reference [..] `namespaces.cc:45:9`!
  
  namespace a {
//          ^ definition [..] a/
//          kind Namespace
    namespace c {
//            ^ definition [..] a/c/
//            kind Namespace
      enum E { E0 };
//         ^ definition [..] a/c/E#
//         kind Enum
//             ^^ definition [..] a/c/E0.
//             kind EnumMember
    }
    namespace c_alias = c;
  }
  
  void f(a::c_alias::E) {
//     ^ definition [..] f(5734375c1c12cb14).
//     kind Function
//       ^ reference [..] a/
//                   ^ reference [..] a/c/E#
    (void)a::c::E::E0;
//        ^ reference [..] a/
//           ^ reference [..] a/c/
//              ^ reference [..] a/c/E#
//                 ^^ reference [..] a/c/E0.
    (void)a::c_alias::E::E0;
//        ^ reference [..] a/
//                    ^ reference [..] a/c/E#
//                       ^^ reference [..] a/c/E0.
  }
