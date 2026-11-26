  template <typename T>
//^^^^^^^^ definition [..] `<file>/type_alias.cc`/
//                   ^ definition local 0
  struct S {};
//       ^ definition [..] S#
  
  typedef Nonexistent<int> Sint;
//                         ^^^^ definition [..] Sint#
  
  struct R {
//       ^ definition [..] R#
    typedef S<Sint> Ssint;
//          ^ reference [..] S#
//            ^^^^ reference [..] Sint#
//                  ^^^^^ definition [..] R#Ssint#
  };
