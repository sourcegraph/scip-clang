  // extra-args: -std=c++20
  
  namespace a {
//          ^ definition [..] a/
  }
  
  // nested namespace definition allowed since C++17
  namespace a::b {
//          ^ definition [..] a/
//             ^ definition [..] a/b/
  }
  
  namespace {
//^^^^^^^^^ definition [..] $ANON/namespaces.cc/
  }
  
  inline namespace xx {
//                 ^^ definition [..] xx/
  }
  
  namespace z {
//          ^ definition [..] z/
  
  inline namespace {
//       ^^^^^^^^^ definition [..] $ANON/namespaces.cc/
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
  };
  }
  
  using C = c::C;
  
  #define EXPAND_TO_NAMESPACE \
//        ^^^^^^^^^^^^^^^^^^^ definition [..] namespaces.cc:34:9#
    namespace from_macro {}
//            ^^^^^^^^^^ definition [..] from_macro/
//            ^^^^^^^^^^ definition [..] from_macro/
  
  EXPAND_TO_NAMESPACE
//^^^^^^^^^^^^^^^^^^^ reference [..] namespaces.cc:34:9#
  
  #define EXPAND_TO_NAMESPACE_2 EXPAND_TO_NAMESPACE
//        ^^^^^^^^^^^^^^^^^^^^^ definition [..] namespaces.cc:39:9#
  
  EXPAND_TO_NAMESPACE_2
//^^^^^^^^^^^^^^^^^^^^^ reference [..] namespaces.cc:39:9#
  
  #define IDENTITY(x) x
//        ^^^^^^^^ definition [..] namespaces.cc:43:9#
  
  IDENTITY(namespace in_macro { })
//^^^^^^^^ reference [..] namespaces.cc:43:9#
//                   ^^^^^^^^ definition [..] in_macro/
