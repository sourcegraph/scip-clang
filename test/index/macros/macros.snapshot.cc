  // extra-args: -std=c++23
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/macros.cc`/
//kind File
  
  #include "macros.h"
//         ^^^^^^^^^^ reference [..] `<file>/macros.h`/
  
  int a = MY_MACRO + MY_MACRO_ALIAS;
//    ^ definition [..] a.
//    kind Variable
//        ^^^^^^^^ reference [..] `macros.h:1:9`!
//                   ^^^^^^^^^^^^^^ reference [..] `macros.h:9:9`!
  
  #undef MY_MACRO
//       ^^^^^^^^ reference [..] `macros.h:1:9`!
  
  #if defined(MY_MACRO) // no reference
  #endif
  
  #define MY_MACRO 1
//        ^^^^^^^^ definition [..] `macros.cc:12:9`!
//        kind Macro
  
  int b = MY_MACRO;
//    ^ definition [..] b.
//    kind Variable
//        ^^^^^^^^ reference [..] `macros.cc:12:9`!
  
  #if defined(MY_MACRO)
//            ^^^^^^^^ reference [..] `macros.cc:12:9`!
  #endif
  
  #ifdef MY_MACRO
//       ^^^^^^^^ reference [..] `macros.cc:12:9`!
  #endif
  
  #ifndef MY_MACRO
//        ^^^^^^^^ reference [..] `macros.cc:12:9`!
  #endif
  
  #ifdef NOT_YET_DEFINED_MACRO
  #elifndef MY_MACRO
//          ^^^^^^^^ reference [..] `macros.cc:12:9`!
  #elifdef MY_MACRO
//         ^^^^^^^^ reference [..] `macros.cc:12:9`!
  #endif
  
  #define NOT_YET_DEFINED_MACRO
//        ^^^^^^^^^^^^^^^^^^^^^ definition [..] `macros.cc:30:9`!
//        kind Macro
  
  #define IDENTITY_MACRO(__x) __x
//        ^^^^^^^^^^^^^^ definition [..] `macros.cc:32:9`!
//        kind Macro
  
  int c = IDENTITY_MACRO(10);
//    ^ definition [..] c.
//    kind Variable
//        ^^^^^^^^^^^^^^ reference [..] `macros.cc:32:9`!
  
  #define MACRO_USING_MACRO \
//        ^^^^^^^^^^^^^^^^^ definition [..] `macros.cc:36:9`!
//        kind Macro
    IDENTITY_MACRO(0)
//  ^^^^^^^^^^^^^^ reference [..] `macros.cc:32:9`!
  
  // two uses of a macro that expands to another macro
  int d = MACRO_USING_MACRO + MACRO_USING_MACRO;
//    ^ definition [..] d.
//    kind Variable
//        ^^^^^^^^^^^^^^^^^ reference [..] `macros.cc:36:9`!
//                            ^^^^^^^^^^^^^^^^^ reference [..] `macros.cc:36:9`!
