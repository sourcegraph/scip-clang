  // extra-args: -std=c++17
  
  int MyGlobal = 3;
  
  int f(int x, int y) {
//          ^ definition local 0
//                 ^ definition local 1
    int z = x + y;
//      ^ definition local 2
//          ^ reference local 0
//              ^ reference local 1
    int arr[2] = {x, y};
//      ^^^ definition local 3
//                ^ reference local 0
//                   ^ reference local 1
    auto [a, b] = arr;
//                ^^^ reference local 3
    return z + a + b + MyGlobal;
//         ^ reference local 2
  }
  
  struct S {
    int x;
    static int y;
  };
  
  int f(S s) {
//        ^ definition local 4
    return s.x + S::y;
//         ^ reference local 4
  }
