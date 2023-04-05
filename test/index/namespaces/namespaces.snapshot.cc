  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/namespaces.cc`/
  
  #include "system_header.h"
//         ^^^^^^^^^^^^^^^^^ reference [..] `<file>/system_header.h`/
  
  namespace a {
//          ^ definition [..] a/
  }
  
  // nested namespace definition allowed since C++17
  namespace a::b {
//          ^ definition [..] a/
//             ^ definition [..] a/b/
  }
  
  namespace {
//^^^^^^^^^ definition [..] `$anonymous_namespace_namespaces.cc`/
  }
  
  inline namespace xx {
//                 ^^ definition [..] xx/
  }
  
  namespace z {
//          ^ definition [..] z/
  
  inline namespace {
//       ^^^^^^^^^ definition [..] z/`$anonymous_namespace_namespaces.cc`/
  }
  
  }
  
  // inline nested namespace definition allowed since C++20
  namespace z::inline y {
//          ^ definition [..] z/
//                    ^ definition [..] z/y/
  }
  
  namespace c {
//          ^ definition [..] c/
  class C {
//      ^ definition [..] c/C#
  };
  }
  
  using C = c::C;
//      ^ definition [..] C#
//          ^ reference [..] c/
//             ^ reference [..] c/C#
  
  #define EXPAND_TO_NAMESPACE \
//        ^^^^^^^^^^^^^^^^^^^ definition [..] `namespaces.cc:36:9`!
    namespace from_macro {}
  
  EXPAND_TO_NAMESPACE
//^^^^^^^^^^^^^^^^^^^ definition [..] from_macro/
//^^^^^^^^^^^^^^^^^^^ reference [..] `namespaces.cc:36:9`!
  
  #define EXPAND_TO_NAMESPACE_2 EXPAND_TO_NAMESPACE
//        ^^^^^^^^^^^^^^^^^^^^^ definition [..] `namespaces.cc:41:9`!
//                              ^^^^^^^^^^^^^^^^^^^ reference [..] `namespaces.cc:36:9`!
  
  EXPAND_TO_NAMESPACE_2
//^^^^^^^^^^^^^^^^^^^^^ definition [..] from_macro/
//^^^^^^^^^^^^^^^^^^^^^ reference [..] `namespaces.cc:41:9`!
  
  #define IDENTITY(x) x
//        ^^^^^^^^ definition [..] `namespaces.cc:45:9`!
  
  IDENTITY(namespace in_macro { })
//^^^^^^^^ definition [..] in_macro/
//^^^^^^^^ reference [..] `namespaces.cc:45:9`!
  
  namespace a {
//          ^ definition [..] a/
    namespace c {
//            ^ definition [..] a/c/
      enum E { E0 };
//         ^ definition [..] a/c/E#
//             ^^ definition [..] a/c/E0.
    }
    namespace c_alias = c;
  }
  
  void f(a::c_alias::E) {
//     ^ definition [..] f(5734375c1c12cb14).
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
