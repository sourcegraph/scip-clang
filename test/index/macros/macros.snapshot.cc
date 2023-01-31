  #include "macros.h"
  
  int a = MY_MACRO;
//        ^^^^^^^^ reference [..] macros.h:1:9#
  
  #undef MY_MACRO
//       ^^^^^^^^ reference [..] macros.h:1:9#
  
  #define MY_MACRO 1
//        ^^^^^^^^ definition [..] macros.cc:7:9#
  
  int b = MY_MACRO;
//        ^^^^^^^^ reference [..] macros.cc:7:9#
  
  #ifdef MY_MACRO
  #endif
  
  #ifndef MY_MACRO
  #endif
  
  #ifdef NOT_YET_DEFINED_MACRO
  #endif
  
  #define NOT_YET_DEFINED_MACRO
//        ^^^^^^^^^^^^^^^^^^^^^ definition [..] macros.cc:20:9#
  
  #define IDENTITY_MACRO(__x) __x
//        ^^^^^^^^^^^^^^ definition [..] macros.cc:22:9#
  
  int c = IDENTITY_MACRO(10);
//        ^^^^^^^^^^^^^^ reference [..] macros.cc:22:9#
  
  #define MACRO_USING_MACRO \
//        ^^^^^^^^^^^^^^^^^ definition [..] macros.cc:26:9#
    IDENTITY_MACRO(0)
  
  int d = MACRO_USING_MACRO;
//        ^^^^^^^^^^^^^^^^^ reference [..] macros.cc:26:9#
