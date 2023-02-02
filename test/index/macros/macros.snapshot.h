  #define MY_MACRO 0
//        ^^^^^^^^ definition [..] macros.h:1:9#
  
  #ifdef MY_MACRO
//       ^^^^^^^^ reference [..] macros.h:1:9#
  #endif
  
  #ifndef MY_MACRO
//        ^^^^^^^^ reference [..] macros.h:1:9#
  #endif
