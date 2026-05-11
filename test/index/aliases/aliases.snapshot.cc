  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/aliases.cc`/
//kind File
  
  namespace a {
//          ^ definition [..] a/
//          kind Namespace
  struct S {};
//       ^ definition [..] a/S#
//       kind Struct
  struct T {};
//       ^ definition [..] a/T#
//       kind Struct
  }
  
  namespace b {
//          ^ definition [..] b/
//          kind Namespace
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
//          kind Namespace
    using S = a::S;
//        ^ definition [..] c/S#
//        kind TypeAlias
//            ^ reference [..] a/
//               ^ reference [..] a/S#
    using T = S;
//        ^ definition [..] c/T#
//        kind TypeAlias
//            ^ reference [..] c/S#
  }
  
  namespace d {
//          ^ definition [..] d/
//          kind Namespace
    using S = a::S;
//        ^ definition [..] d/S#
//        kind TypeAlias
//            ^ reference [..] a/
//               ^ reference [..] a/S#
  }
  
  namespace e {
//          ^ definition [..] e/
//          kind Namespace
    using d::S; // equivalent to `using S = d::S;`
//        ^ reference [..] d/
//           ^ reference [..] d/S#
//           ^ definition [..] e/S#
    using T = e::S;
//        ^ definition [..] e/T#
//        kind TypeAlias
//            ^ reference [..] e/
//               ^ reference [..] e/S#
  
    template <typename X>
//                     ^ definition local 0
//                     kind TypeParameter
    struct R {};
//         ^ definition [..] e/R#
//         kind Struct
  
    void f(R<S>) {}
//       ^ definition [..] e/f(6824106dca99b347).
//       kind Function
//         ^ reference [..] e/R#
//           ^ reference [..] e/S#
  }
  
  typedef a::S aS;
//        ^ reference [..] a/
//           ^ reference [..] a/S#
//             ^^ definition [..] aS#
//             kind TypeAlias
  typedef aS aS1;
//        ^^ reference [..] aS#
//           ^^^ definition [..] aS1#
//           kind TypeAlias
  using aS2 = aS;
//      ^^^ definition [..] aS2#
//      kind TypeAlias
//            ^^ reference [..] aS#
  using aS3 = aS1;
//      ^^^ definition [..] aS3#
//      kind TypeAlias
//            ^^^ reference [..] aS1#
  typedef aS2 aS4;
//        ^^^ reference [..] aS2#
//            ^^^ definition [..] aS4#
//            kind TypeAlias
  
  namespace f {
//          ^ definition [..] f/
//          kind Namespace
  template <typename T>
//                   ^ definition local 1
//                   kind TypeParameter
  struct A {
//       ^ definition [..] f/A#
//       kind Struct
    using B = T;
//        ^ definition [..] f/A#B#
//        kind TypeAlias
//            ^ reference local 1
    using C = B;
//        ^ definition [..] f/A#C#
//        kind TypeAlias
//            ^ reference [..] f/A#B#
  };
  }
  
  enum class LongLongEnum {
//           ^^^^^^^^^^^^ definition [..] LongLongEnum#
//           kind Enum
    X
//  ^ definition [..] LongLongEnum#X.
//  kind EnumMember
  };
  
  namespace h {
//          ^ definition [..] h/
//          kind Namespace
    enum class EvenLongerEnum {
//             ^^^^^^^^^^^^^^ definition [..] h/EvenLongerEnum#
//             kind Enum
      Y
//    ^ definition [..] h/EvenLongerEnum#Y.
//    kind EnumMember
    };
  }
  
  void g() {
//     ^ definition [..] g(49f6e7a06ebc5aa8).
//     kind Function
    // Since C++20
    using enum LongLongEnum;
//             ^^^^^^^^^^^^ reference [..] LongLongEnum#
    using enum h::EvenLongerEnum;
//             ^ reference [..] h/
//                ^^^^^^^^^^^^^^ reference [..] h/EvenLongerEnum#
  }
  
  namespace z {
//          ^ definition [..] z/
//          kind Namespace
    struct U {
//         ^ definition [..] z/U#
//         kind Struct
      template <typename T>
//                       ^ definition local 2
//                       kind TypeParameter
      T identity(T t) { return t; }
//    ^ reference local 2
//      ^^^^^^^^ definition [..] z/U#identity(ada6a8422704cf8a).
//      kind Method
//               ^ reference local 2
//                 ^ definition local 3
//                 kind Parameter
//                             ^ reference local 3
    };
  
    struct V: U {
//         ^ definition [..] z/V#
//         kind Struct
//         relation implementation [..] z/U#
//            ^ reference [..] z/U#
      int identity(int t, int) { return t; }
//        ^^^^^^^^ definition [..] z/V#identity(9b79fb6aee4c0440).
//        kind Method
//                     ^ definition local 4
//                     kind Parameter
//                                      ^ reference local 4
      using U::identity;
//          ^ reference [..] z/U#
//             ^^^^^^^^ reference [..] z/U#identity(ada6a8422704cf8a).
//             ^^^^^^^^ definition [..] z/V#identity(ada6a8422704cf8a).
    };
  
    template <typename T>
//                     ^ definition local 5
//                     kind TypeParameter
    struct W {
//         ^ definition [..] z/W#
//         kind Struct
      V v;
//    ^ reference [..] z/V#
//      ^ definition [..] z/W#v.
//      kind Field
  
      T identity(T t) { return v.identity<T>(t); }
//    ^ reference local 5
//      ^^^^^^^^ definition [..] z/W#identity(ada6a8422704cf8a).
//      kind Method
//               ^ reference local 5
//                 ^ definition local 6
//                 kind Parameter
//                             ^ reference [..] z/W#v.
//                               ^^^^^^^^ reference [..] z/V#identity(9b79fb6aee4c0440).
//                               ^^^^^^^^ reference [..] z/V#identity(ada6a8422704cf8a).
//                                        ^ reference local 5
//                                           ^ reference local 6
    };
  }
  
  namespace i {
//          ^ definition [..] i/
//          kind Namespace
    namespace j {
//            ^ definition [..] i/j/
//            kind Namespace
      void f() {}
//         ^ definition [..] i/j/f(49f6e7a06ebc5aa8).
//         kind Function
  
      template <typename T>
//                       ^ definition local 7
//                       kind TypeParameter
      void ft(T) {}
//         ^^ definition [..] i/j/ft(9b289cee16747614).
//         kind Function
//            ^ reference local 7
  
      template <typename T>
//                       ^ definition local 8
//                       kind TypeParameter
      T zero = 0;
//    ^ reference local 8
//      ^^^^ definition [..] i/j/zero.
//      kind Variable
//      ^^^^ definition [..] i/j/zero.
//      kind Variable
    }
    using j::f;
//        ^ reference [..] i/j/
//           ^ definition [..] i/f(49f6e7a06ebc5aa8).
//           ^ reference [..] i/j/f(49f6e7a06ebc5aa8).
    void g() { f(); }
//       ^ definition [..] i/g(49f6e7a06ebc5aa8).
//       kind Function
//             ^ reference [..] i/f(49f6e7a06ebc5aa8).
  
    using j::ft;
//        ^ reference [..] i/j/
//           ^^ definition [..] i/ft(9b289cee16747614).
//           ^^ reference [..] i/j/ft(9b289cee16747614).
    void gt() { ft<int>(0); }
//       ^^ definition [..] i/gt(49f6e7a06ebc5aa8).
//       kind Function
//              ^^ reference [..] i/ft(9b289cee16747614).
  
    namespace k {
//            ^ definition [..] i/k/
//            kind Namespace
      template <typename T>
//                       ^ definition local 9
//                       kind TypeParameter
      struct S {};
//           ^ definition [..] i/k/S#
//           kind Struct
  
      template <typename T>
//                       ^ definition local 10
//                       kind TypeParameter
      using SAlias = S<T>;
//          ^^^^^^ definition [..] i/k/SAlias#
//          kind TypeAlias
//                   ^ reference [..] i/k/S#
//                     ^ reference local 10
    }
  
    using k::S;
//        ^ reference [..] i/k/
//           ^ definition [..] i/S#
//           ^ reference [..] i/k/S#
    using SS = S<int>;
//        ^^ definition [..] i/SS#
//        kind TypeAlias
//             ^ reference [..] i/S#
  
    using j::zero;
//        ^ reference [..] i/j/
//           ^^^^ definition [..] i/zero.
//           ^^^^ reference [..] i/j/zero.
    static int zero_int = zero<int>;
//             ^^^^^^^^ definition [..] i/zero_int.
//             kind StaticVariable
//                        ^^^^ reference [..] i/zero.
  
    using k::SAlias;
//        ^ reference [..] i/k/
//           ^^^^^^ definition [..] i/SAlias#
//           ^^^^^^ reference [..] i/k/SAlias#
    using SAliasInt = SAlias<int>;
//        ^^^^^^^^^ definition [..] i/SAliasInt#
//        kind TypeAlias
//                    ^^^^^^ reference [..] i/SAlias#
  }
