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
  
  namespace z {
//          ^ definition [..] z/
    struct U {
//         ^ definition [..] z/U#
      template <typename T>
//                       ^ definition local 2
      T identity(T t) { return t; }
//    ^ reference local 2
//      ^^^^^^^^ definition [..] z/U#identity(ada6a8422704cf8a).
//               ^ reference local 2
//                 ^ definition local 3
//                             ^ reference local 3
    };
  
    struct V: U {
//         ^ definition [..] z/V#
//         relation implementation [..] z/U#
//            ^ reference [..] z/U#
      int identity(int t, int) { return t; }
//        ^^^^^^^^ definition [..] z/V#identity(9b79fb6aee4c0440).
//                     ^ definition local 4
//                                      ^ reference local 4
      using U::identity;
//          ^ reference [..] z/U#
//             ^^^^^^^^ reference [..] z/U#identity(ada6a8422704cf8a).
//             ^^^^^^^^ definition [..] z/V#identity(ada6a8422704cf8a).
    };
  
    template <typename T>
//                     ^ definition local 5
    struct W {
//         ^ definition [..] z/W#
      V v;
//    ^ reference [..] z/V#
//      ^ definition [..] z/W#v.
  
      T identity(T t) { return v.identity<T>(t); }
//    ^ reference local 5
//      ^^^^^^^^ definition [..] z/W#identity(ada6a8422704cf8a).
//               ^ reference local 5
//                 ^ definition local 6
//                             ^ reference [..] z/W#v.
//                               ^^^^^^^^ reference [..] z/V#identity(9b79fb6aee4c0440).
//                               ^^^^^^^^ reference [..] z/V#identity(ada6a8422704cf8a).
//                                        ^ reference local 5
//                                           ^ reference local 6
    };
  }
  
  namespace i {
//          ^ definition [..] i/
    namespace j {
//            ^ definition [..] i/j/
      void f() {}
//         ^ definition [..] i/j/f(49f6e7a06ebc5aa8).
  
      template <typename T>
//                       ^ definition local 7
      void ft(T) {}
//         ^^ definition [..] i/j/ft(9b289cee16747614).
//            ^ reference local 7
  
      template <typename T>
//                       ^ definition local 8
      T zero = 0;
//    ^ reference local 8
//      ^^^^ definition [..] i/j/zero.
//      ^^^^ definition [..] i/j/zero.
    }
    using j::f;
//        ^ reference [..] i/j/
//           ^ definition [..] i/f(49f6e7a06ebc5aa8).
//           ^ reference [..] i/j/f(49f6e7a06ebc5aa8).
    void g() { f(); }
//       ^ definition [..] i/g(49f6e7a06ebc5aa8).
//             ^ reference [..] i/f(49f6e7a06ebc5aa8).
  
    using j::ft;
//        ^ reference [..] i/j/
//           ^^ definition [..] i/ft(9b289cee16747614).
//           ^^ reference [..] i/j/ft(9b289cee16747614).
    void gt() { ft<int>(0); }
//       ^^ definition [..] i/gt(49f6e7a06ebc5aa8).
//              ^^ reference [..] i/ft(9b289cee16747614).
  
    namespace k {
//            ^ definition [..] i/k/
      template <typename T>
//                       ^ definition local 9
      struct S {};
//           ^ definition [..] i/k/S#
  
      template <typename T>
//                       ^ definition local 10
      using SAlias = S<T>;
//          ^^^^^^ definition [..] i/k/SAlias#
//                   ^ reference [..] i/k/S#
//                     ^ reference local 10
    }
  
    using k::S;
//        ^ reference [..] i/k/
//           ^ definition [..] i/S#
//           ^ reference [..] i/k/S#
    using SS = S<int>;
//        ^^ definition [..] i/SS#
//             ^ reference [..] i/S#
  
    using j::zero;
//        ^ reference [..] i/j/
//           ^^^^ definition [..] i/zero.
//           ^^^^ reference [..] i/j/zero.
    static int zero_int = zero<int>;
//             ^^^^^^^^ definition [..] i/zero_int.
//                        ^^^^ reference [..] i/j/zero.
  
    using k::SAlias;
//        ^ reference [..] i/k/
//           ^^^^^^ definition [..] i/SAlias#
//           ^^^^^^ reference [..] i/k/SAlias#
    using SAliasInt = SAlias<int>;
//        ^^^^^^^^^ definition [..] i/SAliasInt#
//                    ^^^^^^ reference [..] i/SAlias#
  }
