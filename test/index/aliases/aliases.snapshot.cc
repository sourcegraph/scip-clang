  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/aliases.cc`/
  
  namespace a {
//          ^ definition [..] a/
  struct S {};
//       ^ definition [..] a/S#
  struct T {};
//       ^ definition [..] a/T#
  }
  
  namespace b {
//          ^ definition [..] b/
    using a::S, a::T;
//        ^ reference [..] a/
//           ^ reference [..] a/S#
//           ^ definition [..] b/S#
//              ^ reference [..] a/
//                 ^ reference [..] a/T#
//                 ^ definition [..] b/T#
  }
  
  namespace c {
//          ^ definition [..] c/
    using S = a::S;
//        ^ definition [..] c/S#
//            ^ reference [..] a/
//               ^ reference [..] a/S#
    using T = S;
//        ^ definition [..] c/T#
//            ^ reference [..] c/S#
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
    using d::S; // equivalent to `using S = d::S;`
//        ^ reference [..] d/
//           ^ reference [..] d/S#
//           ^ definition [..] e/S#
    using T = e::S;
//        ^ definition [..] e/T#
//            ^ reference [..] e/
//               ^ reference [..] e/S#
  
    template <typename X>
//                     ^ definition local 0
    struct R {};
//         ^ definition [..] e/R#
  
    void f(R<S>) {}
//       ^ definition [..] e/f(6824106dca99b347).
//         ^ reference [..] e/R#
//           ^ reference [..] e/S#
  }
  
  typedef a::S aS;
//        ^ reference [..] a/
//           ^ reference [..] a/S#
//             ^^ definition [..] aS#
  typedef aS aS1;
//        ^^ reference [..] aS#
//           ^^^ definition [..] aS1#
  using aS2 = aS;
//      ^^^ definition [..] aS2#
//            ^^ reference [..] aS#
  using aS3 = aS1;
//      ^^^ definition [..] aS3#
//            ^^^ reference [..] aS1#
  typedef aS2 aS4;
//        ^^^ reference [..] aS2#
//            ^^^ definition [..] aS4#
  
  namespace f {
//          ^ definition [..] f/
  template <typename T>
//                   ^ definition local 1
  struct A {
//       ^ definition [..] f/A#
    using B = T;
//        ^ definition [..] f/A#B#
//            ^ reference local 1
    using C = B;
//        ^ definition [..] f/A#C#
//            ^ reference [..] f/A#B#
  };
  }
  
  enum class LongLongEnum {
//           ^^^^^^^^^^^^ definition [..] LongLongEnum#
    X
//  ^ definition [..] LongLongEnum#X.
  };
  
  namespace h {
//          ^ definition [..] h/
    enum class EvenLongerEnum {
//             ^^^^^^^^^^^^^^ definition [..] h/EvenLongerEnum#
      Y
//    ^ definition [..] h/EvenLongerEnum#Y.
    };
  }
  
  void g() {
//     ^ definition [..] g(49f6e7a06ebc5aa8).
    // Since C++20
    using enum LongLongEnum;
//             ^^^^^^^^^^^^ reference [..] LongLongEnum#
    using enum h::EvenLongerEnum;
//             ^ reference [..] h/
//                ^^^^^^^^^^^^^^ reference [..] h/EvenLongerEnum#
  }
