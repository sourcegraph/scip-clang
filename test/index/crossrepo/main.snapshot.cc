  #include "external/dep1/dep1.h"
//^ definition main v0$ `<file>/main.cc`/
//         ^^^^^^^^^^^^^^^^^^^^^^ reference dep1 v1$ `<file>/dep1.h`/
  
  int main(int, char **) {
//    ^^^^ definition main v0$ main(afcd3168fc1188a4).
    dep1::f();
//  ^^^^ reference dep1 v1$ dep1/
//        ^ reference dep1 v1$ dep1/f(49f6e7a06ebc5aa8).
    dep1::C *c = dep1::newC();
//  ^^^^ reference dep1 v1$ dep1/
//        ^ reference [..] dep1/C#
//           ^ definition local 0
//               ^^^^ reference dep1 v1$ dep1/
//                     ^^^^ reference [..] dep1/newC(b06852b0fa3d4847).
    dep1::deleteC(c);
//  ^^^^ reference dep1 v1$ dep1/
//        ^^^^^^^ reference [..] dep1/deleteC(89ce2b6b75cd633e).
//                ^ reference local 0
    dep1::S<int> s{};
//  ^^^^ reference dep1 v1$ dep1/
//        ^ reference dep1 v1$ dep1/S#
//               ^ definition local 1
    return s.identity(0);
//         ^ reference local 1
//           ^^^^^^^^ reference dep1 v1$ dep1/S#identity(d08e07a8525eb4c).
  }
