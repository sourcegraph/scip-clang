  #define MY_MACRO 0
//^ definition [..] `<file>/macros.h`/
//        ^^^^^^^^ definition [..] `macros.h:1:9`!
  
  #ifdef MY_MACRO
//       ^^^^^^^^ reference [..] `macros.h:1:9`!
  #endif
  
  #ifndef MY_MACRO
//        ^^^^^^^^ reference [..] `macros.h:1:9`!
  #endif
  
  #define MY_MACRO_ALIAS MY_MACRO
//        ^^^^^^^^^^^^^^ definition [..] `macros.h:9:9`!
//                       ^^^^^^^^ reference [..] `macros.h:1:9`!
