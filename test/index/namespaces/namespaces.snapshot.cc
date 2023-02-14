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
