  #include "myheader.h"
//^ definition [..] `<file>/mydefs.cc`/
//kind File
//         ^^^^^^^^^^^^ reference [..] `<file>/myheader.h`/
  
  void f() {}
//     ^ definition [..] f(49f6e7a06ebc5aa8).
//     kind Function
  
  struct S {};
//       ^ definition [..] S#
//       kind Struct
  
  void C::m() { }
//     ^ reference [..] C#
//        ^ definition [..] C#m(49f6e7a06ebc5aa8).
//        kind Method
  
  int Global = 3;
//    ^^^^^^ definition [..] Global.
//    kind Variable
  
  int externInt = 30;
//    ^^^^^^^^^ definition [..] externInt.
//    kind Variable
