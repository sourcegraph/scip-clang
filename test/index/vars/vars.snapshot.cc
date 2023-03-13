  // extra-args: -std=c++17
  
  int MyGlobal = 3;
  
  int f(int x, int y) {
//          ^ definition local 0
//                 ^ definition local 1
    int z = x + y;
//      ^ definition local 2
    int arr[2] = {x, y};
//      ^^^ definition local 3
    auto [a, b] = arr;
    return z + a + b + MyGlobal;
  }
  
  struct S {
    int x;
    static int y;
  };
  
  int f(S s) {
//        ^ definition local 4
    return s.x + S::y;
  }
