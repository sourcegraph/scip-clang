  // extra-args: -std=c++17
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/vars.cc`/
//kind File
  
  int MyGlobal = 3;
//    ^^^^^^^^ definition [..] MyGlobal.
//    kind Variable
  
  namespace n {
//          ^ definition [..] n/
//          kind Namespace
    int otherGlobal = 0;
//      ^^^^^^^^^^^ definition [..] n/otherGlobal.
//      kind Variable
  }
  
  int f(int x_, int y_);
//    ^ reference [..] f(9b79fb6aee4c0440).
//          ^^ definition local 0
//          kind Parameter
//                  ^^ definition local 1
//                  kind Parameter
  
  int f(int x, int y) {
//    ^ definition [..] f(9b79fb6aee4c0440).
//    kind Function
//          ^ definition local 2
//          kind Parameter
//                 ^ definition local 3
//                 kind Parameter
    static int z = x + y;
//             ^ definition local 4
//             kind Variable
//                 ^ reference local 2
//                     ^ reference local 3
    int arr[2] = {x, y};
//      ^^^ definition local 5
//      kind Variable
//                ^ reference local 2
//                   ^ reference local 3
    auto [a, b] = arr;
//        ^ definition local 6
//        kind Variable
//           ^ definition local 7
//           kind Variable
//                ^^^ reference local 5
    return z + a + b + MyGlobal + n::otherGlobal;
//         ^ reference local 4
//             ^ reference local 6
//                 ^ reference local 7
//                     ^^^^^^^^ reference [..] MyGlobal.
//                                ^ reference [..] n/
//                                   ^^^^^^^^^^^ reference [..] n/otherGlobal.
  }
  
  struct S {
//       ^ definition [..] S#
//       kind Struct
    int x;
//      ^ definition [..] S#x.
//      kind Field
    static int y;
//             ^ definition [..] S#y.
//             kind StaticDataMember
  };
  
  int f(S s) {
//    ^ definition [..] f(6871c211ea8bb0a1).
//    kind Function
//      ^ reference [..] S#
//        ^ definition local 8
//        kind Parameter
    return s.x + S::y;
//         ^ reference local 8
//           ^ reference [..] S#x.
//               ^ reference [..] S#
//                  ^ reference [..] S#y.
  }
  
  void lambdas() {
//     ^^^^^^^ definition [..] lambdas(49f6e7a06ebc5aa8).
//     kind Function
    int x = 0;
//      ^ definition local 9
//      kind Variable
    int y = 1;
//      ^ definition local 10
//      kind Variable
    auto add = [&x, y](int z) mutable {
//       ^^^ definition local 11
//       kind Variable
//               ^ reference local 9
//                  ^ reference local 10
//                         ^ definition local 12
//                         kind Parameter
      y += z;
//    ^ reference local 10
//         ^ reference local 12
      x += y;
//    ^ reference local 9
//         ^ reference local 10
    };
  }
