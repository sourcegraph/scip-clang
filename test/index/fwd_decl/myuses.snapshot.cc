  #include "myheader.h"
//^ definition [..] `<file>/myuses.cc`/
//         ^^^^^^^^^^^^ reference [..] `<file>/myheader.h`/
  #include "ext_header.h"
//         ^^^^^^^^^^^^^^ reference [..] `<file>/ext_header.h`/
  
  void doStuff(S *, C *c) {
//     ^^^^^^^ definition [..] doStuff(87adf7c4c0c37b30).
//             ^ reference [..] S#
//                  ^ reference [..] C#
//                     ^ definition local 0
    f();
//  ^ reference [..] f(49f6e7a06ebc5aa8).
    c->m();
//  ^ reference local 0
//     ^ reference [..] C#m(49f6e7a06ebc5aa8).
    if (Global == 1) {
//      ^^^^^^ reference [..] Global.
      perform_magic();
//    ^^^^^^^^^^^^^ reference [..] perform_magic(49f6e7a06ebc5aa8).
    } else {
      undo_magic();
//    ^^^^^^^^^^ reference [..] undo_magic(49f6e7a06ebc5aa8).
    }
  }
  
  int useExtern() {
//    ^^^^^^^^^ definition [..] useExtern(b126dc7c1de90089).
    extern int externInt;
//             ^^^^^^^^^ reference [..] externInt.
    return externInt;
//         ^^^^^^^^^ reference [..] externInt.
  }
