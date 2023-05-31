  #include "myheader.h"
//^ definition [..] `<file>/mydefs.cc`/
//         ^^^^^^^^^^^^ reference [..] `<file>/myheader.h`/
  
  void f() {}
//     ^ definition [..] f(49f6e7a06ebc5aa8).
  
  struct S {};
//       ^ definition [..] S#
  
  void C::m() { }
//     ^ reference [..] C#
//        ^ definition [..] C#m(49f6e7a06ebc5aa8).
  
  int Global = 3;
//    ^^^^^^ definition [..] Global.
  
  int externInt = 30;
//    ^^^^^^^^^ definition [..] externInt.
