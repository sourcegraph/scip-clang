  namespace a {
//^^^^^^^^^ definition [..] `<file>/using.cc`/
//          ^ definition [..] a/
  struct S {};
//       ^ definition [..] a/S#
  }
  
  namespace b {
//          ^ definition [..] b/
    using a::S;
//        ^ reference [..] a/
  }
  
  namespace c {
//          ^ definition [..] c/
    using S = a::S;
//        ^ definition [..] c/S#
//            ^ reference [..] a/
//               ^ reference [..] a/S#
    using T = S;
//        ^ definition [..] c/T#
  }
  
  namespace d {
//          ^ definition [..] d/
    using S = a::S;
//        ^ definition [..] d/S#
//            ^ reference [..] a/
//               ^ reference [..] a/S#
  }
  
  namespace e {
//          ^ definition [..] e/
    using d::S;
//        ^ reference [..] d/
  
    template <typename X>
//                     ^ definition local 0
    struct R {};
//         ^ definition [..] e/R#
  
    void f(R<S>) {}
//       ^ definition [..] e/f(6824106dca99b347).
//         ^ reference [..] e/R#
//           ^ reference [..] a/S#
  }
  
  typedef a::S aS;
//        ^ reference [..] a/
//           ^ reference [..] a/S#
//             ^^ definition [..] aS#
  typedef aS aS1;
//           ^^^ definition [..] aS1#
  using aS2 = aS;
//      ^^^ definition [..] aS2#
  using aS3 = aS1;
//      ^^^ definition [..] aS3#
  typedef aS2 aS4;
//            ^^^ definition [..] aS4#
