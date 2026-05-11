  #pragma GCC system_header
//^ definition [..] `<file>/system_header.h`/
//kind File
  
  #define SYSTEM_INT 0
//        ^^^^^^^^^^ definition [..] `system_header.h:3:9`!
//        kind Macro
  
  #define OTHER_SYSTEM_INT (SYSTEM_INT + 1)
//        ^^^^^^^^^^^^^^^^ definition [..] `system_header.h:5:9`!
//        kind Macro
//                          ^^^^^^^^^^ reference [..] `system_header.h:3:9`!
