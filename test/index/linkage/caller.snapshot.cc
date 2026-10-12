  #include "api.h"
//^ definition [..] `<file>/caller.cc`/
//         ^^^^^^^ reference [..] `<file>/api.h`/
  int use_apis() {
//    ^^^^^^^^ definition [..] use_apis(b126dc7c1de90089).
    CValue value{1};
//  ^^^^^^ reference [..] CValue#
//         ^^^^^ definition local 0
    return c_api(value.value) + c_counter + cpp_api(2) + cpp_api(3.5)
//         ^^^^^ reference [..] c_api(7864480464b09eea).
//               ^^^^^ reference local 0
//                     ^^^^^ reference [..] CValue#value.
//                              ^^^^^^^^^ reference [..] c_counter.
//                                          ^^^^^^^ reference [..] cpp_api(7864480464b09eea).
//                                                       ^^^^^^^ reference [..] cpp_api(620b9ac44d2573a6).
         + nested::nested_api(4) + single_api(5);
//         ^^^^^^ reference [..] nested/
//                 ^^^^^^^^^^ reference [..] nested/nested_api(7864480464b09eea).
//                                 ^^^^^^^^^^ reference [..] single_api(7864480464b09eea).
  }
