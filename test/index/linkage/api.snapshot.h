  extern "C" {
//^^^^^^ definition [..] `<file>/api.h`/
  int c_api(int value);
//    ^^^^^ reference [..] c_api(7864480464b09eea).
//              ^^^^^ definition local 0
  extern int c_counter;
//           ^^^^^^^^^ definition [..] c_counter.
  struct CValue { int value; };
//       ^^^^^^ definition [..] CValue#
//                    ^^^^^ definition [..] CValue#value.
  extern "C++" {
  int cpp_api(int value);
//    ^^^^^^^ reference [..] cpp_api(7864480464b09eea).
//                ^^^^^ definition local 1
  int cpp_api(double value);
//    ^^^^^^^ reference [..] cpp_api(620b9ac44d2573a6).
//                   ^^^^^ definition local 2
  }
  }
  namespace nested {
//          ^^^^^^ definition [..] nested/
  extern "C" {
  int nested_api(int value);
//    ^^^^^^^^^^ reference [..] nested/nested_api(7864480464b09eea).
//                   ^^^^^ definition local 3
  }
  }
  extern "C" int single_api(int value);
//               ^^^^^^^^^^ reference [..] single_api(7864480464b09eea).
//                              ^^^^^ definition local 4
