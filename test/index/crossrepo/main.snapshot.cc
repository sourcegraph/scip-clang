  #include "external/dep1/dep1.h"
//^ definition main v0 $`<file>/main.cc`/
//         ^^^^^^^^^^^^^^^^^^^^^^ reference dep1 v1 $`<file>/dep1.h`/
  
  int main(int, char **) {
//    ^^^^ definition main v0 $main(afcd3168fc1188a4).
    dep1::f();
//  ^^^^ reference dep1 v1 $dep1/
//        ^ reference [..] dep1/f(49f6e7a06ebc5aa8).
    return 0;
  }
