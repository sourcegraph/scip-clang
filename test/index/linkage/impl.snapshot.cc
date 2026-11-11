  #include "api.h"
//^ definition [..] `<file>/impl.cc`/
//         ^^^^^^^ reference [..] `<file>/api.h`/
  int c_api(int value) { return value; }
//    ^^^^^ definition [..] c_api(7864480464b09eea).
//              ^^^^^ definition local 0
//                              ^^^^^ reference local 0
  int c_counter = 7;
//    ^^^^^^^^^ definition [..] c_counter.
  int cpp_api(int value) { return value; }
//    ^^^^^^^ definition [..] cpp_api(7864480464b09eea).
//                ^^^^^ definition local 1
//                                ^^^^^ reference local 1
  int cpp_api(double value) { return static_cast<int>(value); }
//    ^^^^^^^ definition [..] cpp_api(620b9ac44d2573a6).
//                   ^^^^^ definition local 2
//                                                    ^^^^^ reference local 2
  namespace nested {
//          ^^^^^^ definition [..] nested/
  int nested_api(int value) { return value; }
//    ^^^^^^^^^^ definition [..] nested/nested_api(7864480464b09eea).
//                   ^^^^^ definition local 3
//                                   ^^^^^ reference local 3
  }
  int single_api(int value) { return value; }
//    ^^^^^^^^^^ definition [..] single_api(7864480464b09eea).
//                   ^^^^^ definition local 4
//                                   ^^^^^ reference local 4
