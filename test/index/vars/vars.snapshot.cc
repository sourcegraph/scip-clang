  // extra-args: -std=c++17
  
  int MyGlobal = 3;
  
  int f(int x_, int y_);
//          ^^ definition local 0
//                  ^^ definition local 1
  
  int f(int x, int y) {
//          ^ definition local 2
//                 ^ definition local 3
    int z = x + y;
//      ^ definition local 4
//          ^ reference local 2
//              ^ reference local 3
    int arr[2] = {x, y};
//      ^^^ definition local 5
//                ^ reference local 2
//                   ^ reference local 3
    auto [a, b] = arr;
//        ^ definition local 6
//           ^ definition local 7
//                ^^^ reference local 5
    return z + a + b + MyGlobal;
//         ^ reference local 4
//             ^ reference local 6
//                 ^ reference local 7
  }
  
  struct S {
//       ^ definition [..] S#
    int x;
    static int y;
  };
  
  int f(S s) {
//        ^ definition local 8
    return s.x + S::y;
//         ^ reference local 8
  }
